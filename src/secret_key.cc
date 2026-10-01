// SHELTER — мастер-ключ для шифрования секретов UI (пароли, ключи ИИ).
// Windows: ключ защищён DPAPI (привязан к учётной записи пользователя).
// macOS/Linux: файл secret.key с правами 0600 в каталоге данных пользователя.
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <random>
#include <string>

#include "src/platform.h"

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace shelter {
namespace platform {

namespace {

constexpr size_t kKeyLen = 32;

std::string ToHex(const std::string& b) {
  static const char* d = "0123456789abcdef";
  std::string o;
  for (unsigned char c : b) {
    o.push_back(d[c >> 4]);
    o.push_back(d[c & 15]);
  }
  return o;
}

bool ReadAll(const std::filesystem::path& p, std::string* out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

bool WriteAll(const std::filesystem::path& p, const std::string& data) {
#if defined(_WIN32)
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), (std::streamsize)data.size());
  return (bool)f;
#else
  int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return false;
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n <= 0) break;
    off += (size_t)n;
  }
  ::close(fd);
  ::chmod(p.c_str(), 0600);
  return off == data.size();
#endif
}

#if defined(_WIN32)
bool Protect(const std::string& in, std::string* out) {
  DATA_BLOB i{(DWORD)in.size(), (BYTE*)in.data()}, o{};
  if (!CryptProtectData(&i, L"SHELTER", nullptr, nullptr, nullptr, 0, &o)) return false;
  out->assign((const char*)o.pbData, o.cbData);
  LocalFree(o.pbData);
  return true;
}
bool Unprotect(const std::string& in, std::string* out) {
  DATA_BLOB i{(DWORD)in.size(), (BYTE*)in.data()}, o{};
  if (!CryptUnprotectData(&i, nullptr, nullptr, nullptr, nullptr, 0, &o)) return false;
  out->assign((const char*)o.pbData, o.cbData);
  LocalFree(o.pbData);
  return true;
}
#else
bool Protect(const std::string& in, std::string* out) { *out = in; return true; }
bool Unprotect(const std::string& in, std::string* out) { *out = in; return true; }
#endif

}  // namespace

std::string SecretKeyHex() {
  static std::string cached;
  static bool done = false;
  if (done) return cached;
  done = true;

  std::error_code ec;
  std::filesystem::path dir = std::filesystem::path(UserDataDir());
  std::filesystem::create_directories(dir, ec);
  const std::filesystem::path path = dir / "secret.key";

  std::string key, raw;
  if (ReadAll(path, &raw) && !raw.empty() && Unprotect(raw, &key) && key.size() == kKeyLen) {
    cached = ToHex(key);
    return cached;
  }
  std::random_device rd;
  key.clear();
  for (size_t i = 0; i < kKeyLen; ++i) key.push_back((char)(rd() & 0xff));
  std::string blob;
  if (Protect(key, &blob)) WriteAll(path, blob);
  cached = ToHex(key);
  return cached;
}

}  // namespace platform
}  // namespace shelter
