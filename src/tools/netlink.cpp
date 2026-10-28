// SPDX-License-Identifier: GPL-2.0
#include "netlink.hpp"
#include "status.hpp"

#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <cstring>
#include <vector>

namespace tosya {
namespace {

/*
 * The NDK UAPI headers only ship NLA_ALIGN/NLA_HDRLEN plus macros that do not
 * handle const pointers, so iterate explicitly with a byte cursor and these two
 * predicates.
 */
[[nodiscard]] bool nlmsg_ok(const nlmsghdr *header, int left) {
  return left >= static_cast<int>(sizeof(nlmsghdr)) &&
         header->nlmsg_len >= sizeof(nlmsghdr) &&
         static_cast<int>(header->nlmsg_len) <= left;
}

[[nodiscard]] bool nlattr_ok(const nlattr *attr, int left) {
  return left >= static_cast<int>(sizeof(nlattr)) &&
         attr->nla_len >= sizeof(nlattr) &&
         static_cast<int>(attr->nla_len) <= left;
}

[[nodiscard]] const nlmsghdr *nlmsg_next(const nlmsghdr *header) {
  return reinterpret_cast<const nlmsghdr *>(
      reinterpret_cast<const std::byte *>(header) +
      NLMSG_ALIGN(header->nlmsg_len));
}

[[nodiscard]] const nlattr *nlattr_next(const nlattr *attr) {
  return reinterpret_cast<const nlattr *>(
      reinterpret_cast<const std::byte *>(attr) + NLA_ALIGN(attr->nla_len));
}

/* Little endian, matching the kernel side (see src/netlink.c). */
void store_u32(std::span<std::byte> out, std::size_t index,
               std::uint32_t value) {
  out[index + 0] = static_cast<std::byte>(value & 0xff);
  out[index + 1] = static_cast<std::byte>((value >> 8) & 0xff);
  out[index + 2] = static_cast<std::byte>((value >> 16) & 0xff);
  out[index + 3] = static_cast<std::byte>((value >> 24) & 0xff);
}

/* A netlink buffer with typed views into its header area. */
struct Buffer {
  std::vector<std::byte> bytes;

  [[nodiscard]] nlmsghdr *nlmsg() {
    return reinterpret_cast<nlmsghdr *>(bytes.data());
  }
  [[nodiscard]] genlmsghdr *genlmsg() {
    return reinterpret_cast<genlmsghdr *>(bytes.data() + NLMSG_HDRLEN);
  }
};

} // namespace

bool NetlinkClient::exchange(std::span<const std::byte> request,
                             std::span<std::byte> reply) {
  sockaddr_nl address{};
  address.nl_family = AF_NETLINK;

  iovec iov{.iov_base = const_cast<std::byte *>(request.data()),
            .iov_len = request.size()};
  msghdr message{};
  message.msg_name = &address;
  message.msg_namelen = sizeof(address);
  message.msg_iov = &iov;
  message.msg_iovlen = 1;

  if (::sendmsg(socket_.get(), &message, 0) < 0) {
    Log::warn("sendmsg: {}", std::strerror(errno));
    return false;
  }

  iov.iov_base = reply.data();
  iov.iov_len = reply.size();
  const ssize_t received = ::recvmsg(socket_.get(), &message, 0);
  if (received < 0) {
    /* EAGAIN is the read deadline firing: the module is gone or never answered.
     */
    Log::warn("recvmsg: {}", errno == EAGAIN
                                 ? "timed out waiting for the kernel"
                                 : std::strerror(errno));
    return false;
  }

  reply_bytes_ = static_cast<std::size_t>(received);
  const auto *header = reinterpret_cast<const nlmsghdr *>(reply.data());
  bool answered = false;
  for (int left = static_cast<int>(received); nlmsg_ok(header, left);) {
    const int step = static_cast<int>(NLMSG_ALIGN(header->nlmsg_len));
    if (header->nlmsg_seq != seq_) {
      /* A late reply to a request that already timed out; ignore it. */
      left -= step;
      header = nlmsg_next(header);
      continue;
    }
    if (header->nlmsg_type == NLMSG_ERROR) {
      const auto *error =
          reinterpret_cast<const nlmsgerr *>(NLMSG_DATA(header));
      if (error->error != 0) {
        /* A module older than this tool does not know the command; that is
         * worth telling apart from a module that is not there at all. */
        if (error->error == -EOPNOTSUPP || error->error == -EPROTONOSUPPORT)
          unsupported_ = true;
        Log::warn("netlink error: {}", std::strerror(-error->error));
        return false;
      }
    }
    answered = true;
    left -= step;
    header = nlmsg_next(header);
  }
  return answered;
}

std::optional<std::uint16_t> NetlinkClient::resolve_family() {
  if (!ensure_connected())
    return std::nullopt;

  Buffer request{};
  const std::size_t name_len = kFamilyName.size() + 1;
  request.bytes.assign(NLMSG_SPACE(GENL_HDRLEN) + NLA_HDRLEN + name_len,
                       std::byte{0});

  auto *nlh = request.nlmsg();
  nlh->nlmsg_len =
      NLMSG_LENGTH(GENL_HDRLEN + NLA_HDRLEN + static_cast<int>(name_len));
  nlh->nlmsg_type = GENL_ID_CTRL;
  nlh->nlmsg_flags = NLM_F_REQUEST;
  nlh->nlmsg_seq = ++seq_;

  auto *genl = request.genlmsg();
  genl->cmd = CTRL_CMD_GETFAMILY;
  genl->version = 1;

  auto *attr = reinterpret_cast<nlattr *>(reinterpret_cast<std::byte *>(genl) +
                                          GENL_HDRLEN);
  attr->nla_type = CTRL_ATTR_FAMILY_NAME;
  attr->nla_len = NLA_HDRLEN + static_cast<int>(name_len);
  std::memcpy(reinterpret_cast<std::byte *>(attr) + NLA_HDRLEN,
              kFamilyName.data(), name_len);

  std::vector<std::byte> reply(kReplySize);
  if (!exchange(request.bytes, reply))
    return std::nullopt;

  const auto *header = reinterpret_cast<const nlmsghdr *>(reply.data());
  for (int left = static_cast<int>(reply.size()); nlmsg_ok(header, left);) {
    if (header->nlmsg_type == NLMSG_ERROR)
      break;

    const auto *genl = reinterpret_cast<const genlmsghdr *>(NLMSG_DATA(header));
    int attr_left = static_cast<int>(header->nlmsg_len) -
                    static_cast<int>(NLMSG_LENGTH(GENL_HDRLEN));
    const auto *attr = reinterpret_cast<const nlattr *>(
        reinterpret_cast<const std::byte *>(genl) + GENL_HDRLEN);
    while (nlattr_ok(attr, attr_left)) {
      if (attr->nla_type == CTRL_ATTR_FAMILY_ID) {
        std::uint16_t id = 0;
        std::memcpy(&id, reinterpret_cast<const std::byte *>(attr) + NLA_HDRLEN,
                    sizeof(id));
        return id;
      }
      attr_left -= static_cast<int>(NLA_ALIGN(attr->nla_len));
      attr = nlattr_next(attr);
    }

    left -= static_cast<int>(NLMSG_ALIGN(header->nlmsg_len));
    header = nlmsg_next(header);
  }

  if (!warned_) {
    warned_ = true;
    Log::warn("generic netlink family {} not found (module not loaded?)",
              kFamilyName);
  }
  return std::nullopt;
}

bool NetlinkClient::send_command(std::uint8_t cmd,
                                 std::span<const std::byte> blob) {
  if (!ensure_connected())
    return false;

  if (!family_) {
    const auto resolved = resolve_family();
    if (!resolved)
      return false;
    family_ = resolved;
  }

  Buffer request{};
  request.bytes.assign(NLMSG_SPACE(GENL_HDRLEN) +
                           NLA_ALIGN(NLA_HDRLEN + blob.size()),
                       std::byte{0});

  auto *nlh = request.nlmsg();
  nlh->nlmsg_len =
      NLMSG_LENGTH(GENL_HDRLEN + NLA_HDRLEN + static_cast<int>(blob.size()));
  nlh->nlmsg_type = *family_;
  nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  nlh->nlmsg_seq = ++seq_;

  auto *genl = request.genlmsg();
  genl->cmd = cmd;
  genl->version = KAUX_FAMILY_VERSION;

  auto *attr = reinterpret_cast<nlattr *>(reinterpret_cast<std::byte *>(genl) +
                                          GENL_HDRLEN);
  attr->nla_type = KAUX_ATTR_BLOB;
  attr->nla_len = NLA_HDRLEN + static_cast<int>(blob.size());
  std::memcpy(reinterpret_cast<std::byte *>(attr) + NLA_HDRLEN, blob.data(),
              blob.size());

  std::vector<std::byte> reply(kReplySize);
  return exchange(std::span{request.bytes}.first(nlh->nlmsg_len), reply);
}

std::optional<kaux_status> NetlinkClient::status() {
  if (!ensure_connected())
    return std::nullopt;
  if (!family_) {
    const auto resolved = resolve_family();
    if (!resolved)
      return std::nullopt;
    family_ = resolved;
  }

  Buffer request{};
  request.bytes.assign(NLMSG_SPACE(GENL_HDRLEN), std::byte{0});
  auto *nlh = request.nlmsg();
  nlh->nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
  nlh->nlmsg_type = *family_;
  nlh->nlmsg_flags = NLM_F_REQUEST;
  nlh->nlmsg_seq = ++seq_;
  request.genlmsg()->cmd = KAUX_CMD_STATUS;
  request.genlmsg()->version = KAUX_FAMILY_VERSION;

  std::vector<std::byte> reply(kReplySize);
  unsupported_ = false;
  if (!exchange(std::span{request.bytes}.first(nlh->nlmsg_len), reply)) {
    if (unsupported_)
      Log::warn("the loaded module does not know KAUX_CMD_STATUS; it is older "
                "than this tool (a reboot loads the new one)");
    return std::nullopt;
  }

  const auto view = std::span<const std::byte>(reply.data(), reply_bytes_);
  int left = static_cast<int>(view.size());
  for (const std::byte *cursor = view.data();
       nlmsg_ok(reinterpret_cast<const nlmsghdr *>(cursor), left);) {
    const auto *header = reinterpret_cast<const nlmsghdr *>(cursor);
    const int step = static_cast<int>(NLMSG_ALIGN(header->nlmsg_len));
    if (header->nlmsg_type != NLMSG_ERROR && header->nlmsg_seq == seq_) {
      const auto *genl =
          reinterpret_cast<const genlmsghdr *>(NLMSG_DATA(header));
      int attr_left = static_cast<int>(header->nlmsg_len) -
                      static_cast<int>(NLMSG_LENGTH(GENL_HDRLEN));
      const auto *attr = reinterpret_cast<const nlattr *>(
          reinterpret_cast<const std::byte *>(genl) + GENL_HDRLEN);
      for (; nlattr_ok(attr, attr_left); attr = nlattr_next(attr)) {
        const int alen = static_cast<int>(attr->nla_len) - NLA_HDRLEN;
        attr_left -= static_cast<int>(NLA_ALIGN(attr->nla_len));
        if (attr->nla_type != KAUX_ATTR_STATUS)
          continue;
        if (alen != static_cast<int>(sizeof(kaux_status))) {
          Log::warn("status of {} byte(s), expected {}", alen,
                    sizeof(kaux_status));
          return std::nullopt;
        }
        kaux_status st{};
        std::memcpy(&st, reinterpret_cast<const std::byte *>(attr) + NLA_HDRLEN,
                    sizeof(st));
        if (st.magic != KAUX_STATUS_MAGIC || st.size != sizeof(st) ||
            st.version != KAUX_FAMILY_VERSION) {
          Log::warn(
              /* No release number in here: the protocol version is what has to
               * match, and saying which one this tool speaks is enough for a
               * reader to act on. */
              "status does not look like ours (magic {:x}, size {}, version "
              "{}); this tool "
              "speaks version {}, so module and tool have to come from the "
              "same build",
              st.magic, st.size, st.version, KAUX_FAMILY_VERSION);
          return std::nullopt;
        }
        return st;
      }
      break;
    }
    left -= step;
    cursor += step;
  }
  return std::nullopt;
}

bool NetlinkClient::ensure_connected() {
  if (socket_.valid())
    return true;

  const int fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
  if (fd < 0) {
    Log::warn("socket: {}", std::strerror(errno));
    return false;
  }

  sockaddr_nl address{};
  address.nl_family = AF_NETLINK;
  if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
    Log::warn("bind: {}", std::strerror(errno));
    ::close(fd);
    return false;
  }

  /*
   * Both directions need a deadline. If the module is unloaded between our
   * request and the reply there is nothing to receive, and without one the
   * helper would sit in recvmsg() forever: that is the "cannot connect after
   * rmmod/insmod" symptom.
   */
  timeval deadline{.tv_sec = 0, .tv_usec = 500'000};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &deadline, sizeof(deadline));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &deadline, sizeof(deadline));

  socket_.reset(fd);
  return true;
}

void NetlinkClient::note_reachable() {
  if (!warned_)
    return;
  warned_ = false;
  Log::info("kernel side reachable again");
}

/*
 * One BEGIN / BATCH... / END sequence. A stream that stops half way stops on
 * the kernel side too: the policy and the names are replaced at END, the apk
 * records land one by one, and an END-less stream is discarded after five
 * seconds of idleness. A lost reply does not say which records ran, so the
 * caller re-sends the whole stream on the next attempt.
 */
bool NetlinkClient::stream(std::uint32_t kind,
                           std::span<const std::vector<std::byte>> records,
                           std::string_view what) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (ensure_connected()) {
      if (!family_) {
        const auto resolved = resolve_family();
        if (!resolved) {
          family_.reset();
          socket_.reset();
          continue;
        }
        family_ = resolved;
      }

      std::vector<std::byte> begin(sizeof(std::uint32_t));
      store_u32(begin, 0, kind);
      bool ok = send_command(KAUX_CMD_BEGIN, begin);

      std::size_t at = 0;
      while (ok && at < records.size()) {
        /* Pack whole records up to the kernel's per-message cap. Every record
         * here is far below it -- a base.apk path is nowhere near 32 KiB -- so
         * a batch always takes at least one. */
        std::size_t bytes = 0, count = 0;
        while (at + count < records.size() &&
               bytes + records[at + count].size() <= kBatchRecordsBytes) {
          bytes += records[at + count].size();
          ++count;
        }
        if (count == 0) {
          Log::warn("{}: a record of {} byte(s) does not fit one batch", what,
                    records[at].size());
          ok = false;
          break;
        }

        std::vector<std::byte> batch(4 + bytes);
        store_u32(batch, 0, static_cast<std::uint32_t>(count));
        std::size_t into = 4;
        for (std::size_t i = 0; i < count; ++i) {
          std::memcpy(batch.data() + into, records[at + i].data(),
                      records[at + i].size());
          into += records[at + i].size();
        }
        ok = send_command(KAUX_CMD_BATCH, batch);
        at += count;
      }

      if (ok)
        ok = send_command(KAUX_CMD_END, {});

      if (ok) {
        note_reachable();
        return true;
      }
    }

    /* A failed batch leaves the stream open on the kernel until its deadline;
     * reconnecting does not close it, but the next BEGIN after the timeout
     * takes the stream over. Re-sending the whole stream is the recovery. */
    family_.reset();
    socket_.reset();
  }

  if (!warned_) {
    warned_ = true;
    Log::warn("kernel side unreachable, {} not updated", what);
    report_status("kernel unreachable");
  }
  return false;
}

bool NetlinkClient::refresh() {
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (ensure_connected()) {
      if (!family_) {
        const auto resolved = resolve_family();
        if (!resolved) {
          family_.reset();
          socket_.reset();
          continue;
        }
        family_ = resolved;
      }
      /* No blob: the kernel resolves the protected parents again. */
      if (send_command(KAUX_CMD_REFRESH, {})) {
        note_reachable();
        return true;
      }
    }
    family_.reset();
    socket_.reset();
  }
  return false;
}

bool NetlinkClient::push_token(std::span<const std::uint32_t, 4> words) {
  /* The kernel reads a token batch as four one-word records after the count
   * header, and refuses anything else: count must be 4 and the blob must be
   * five words. One record per word, so the packed batch is exactly that. */
  std::vector<std::byte> records[4];
  for (std::size_t i = 0; i < 4; ++i) {
    records[i].resize(sizeof(std::uint32_t));
    store_u32(records[i], 0, words[i]);
  }
  return stream(KAUX_KIND_TOKEN, records, "the token");
}

bool NetlinkClient::push(std::span<const Pair> pairs) {
  /* ADD records only: END replaces the whole set, so an empty list clears it
   * -- no placeholder pair needed the way the staging protocol wanted. */
  std::vector<std::vector<std::byte>> records;
  records.reserve(pairs.size());
  for (const auto &pair : pairs) {
    std::vector<std::byte> rec(KAUX_POLICY_REC_WORDS * 4);
    store_u32(rec, 0, KAUX_OP_ADD);
    store_u32(rec, 4, pair.caller);
    store_u32(rec, 8, pair.target);
    store_u32(rec, 12, 0); /* flags: the kernel reads caller and target only */
    records.push_back(std::move(rec));
  }
  return stream(KAUX_KIND_POLICY, records, "the policy");
}

bool NetlinkClient::push_names(std::span<const HiddenName> names) {
  /* ADD records only, END replaces the set. Names travel in the clear because
   * they never leave kernel memory; the synthetic names the guard asks the
   * filesystem about are derived from the token, not from these. */
  std::vector<std::vector<std::byte>> records;
  records.reserve(names.size());
  for (const auto &hidden : names) {
    const auto padded = (hidden.name.size() + 3u) & ~std::size_t{3u};
    std::vector<std::byte> rec(KAUX_NAME_REC_WORDS * 4 + padded);
    store_u32(rec, 0, KAUX_OP_ADD);
    store_u32(rec, 4, hidden.caller);
    store_u32(rec, 8, static_cast<std::uint32_t>(hidden.name.size()));
    std::memcpy(rec.data() + KAUX_NAME_REC_WORDS * 4, hidden.name.data(),
                hidden.name.size()); /* the pad bytes stay zero */
    records.push_back(std::move(rec));
  }
  return stream(KAUX_KIND_NAMES, records, "the hidden-name set");
}

bool NetlinkClient::push_apks(std::span<const ApkEntry> entries) {
  /* One record per apk or library, applied as it arrives. A drop carries no
   * path -- its (dev, ino) is the identity -- and END reports the per-record
   * failures through the status counters, which the caller reads back. */
  std::vector<std::vector<std::byte>> records;
  records.reserve(entries.size());
  for (const auto &entry : entries) {
    const bool add = entry.action == 0;
    const auto path_bytes = add ? entry.path.size() + 1u : 0u;
    const auto padded = (path_bytes + 3u) & ~std::size_t{3u};
    std::vector<std::byte> rec(KAUX_APK_REC_WORDS * 4 + padded);
    store_u32(rec, 0, add ? KAUX_OP_ADD : KAUX_OP_DEL);
    store_u32(rec, 4, entry.uid);
    store_u32(rec, 8, entry.dev);
    store_u32(rec, 12, static_cast<std::uint32_t>(entry.ino & 0xffffffffu));
    store_u32(rec, 16, static_cast<std::uint32_t>(entry.ino >> 32));
    store_u32(rec, 20, static_cast<std::uint32_t>(path_bytes));
    if (add)
      std::memcpy(rec.data() + KAUX_APK_REC_WORDS * 4, entry.path.c_str(),
                  path_bytes); /* the NUL and the pad stay zero */
    records.push_back(std::move(rec));
  }
  return stream(KAUX_KIND_APKS, records, "the caller apk table");
}

} // namespace tosya
