#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>

#ifdef HAVE_LIBMAGIC
#include <magic.h>
#endif

class MimeTypeDetector {
private:
#ifdef HAVE_LIBMAGIC
  magic_t magic_cookie;
  bool libmagic_available;
#endif

  // Fallback extension-to-MIME-type mapping
  static const std::unordered_map<std::string, std::string> &getExtensionMap() {
    static const std::unordered_map<std::string, std::string> mimeTypes = {
        // Images
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".bmp", "image/bmp"},
        {".webp", "image/webp"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},

        // Documents
        {".pdf", "application/pdf"},
        {".doc", "application/msword"},
        {".docx",
         "application/"
         "vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".xls", "application/vnd.ms-excel"},
        {".xlsx",
         "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".ppt", "application/vnd.ms-powerpoint"},
        {".pptx",
         "application/"
         "vnd.openxmlformats-officedocument.presentationml.presentation"},

        // Text
        {".txt", "text/plain"},
        {".csv", "text/csv"},
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "text/javascript"},
        {".json", "application/json"},
        {".xml", "application/xml"},

        // Video
        {".mp4", "video/mp4"},
        {".avi", "video/x-msvideo"},
        {".mov", "video/quicktime"},
        {".wmv", "video/x-ms-wmv"},
        {".mkv", "video/x-matroska"},

        // Audio
        {".mp3", "audio/mpeg"},
        {".wav", "audio/wav"},
        {".ogg", "audio/ogg"},
        {".m4a", "audio/mp4"},

        // Archives
        {".zip", "application/zip"},
        {".rar", "application/x-rar-compressed"},
        {".7z", "application/x-7z-compressed"},
        {".tar", "application/x-tar"},
        {".gz", "application/gzip"},
    };
    return mimeTypes;
  }

  std::string getMimeByExtension(const std::string &filepath) {
    std::filesystem::path p(filepath);
    std::string ext = p.extension().string();

    // Convert to lowercase for case-insensitive matching
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    auto &mimeMap = getExtensionMap();
    auto it = mimeMap.find(ext);
    return it != mimeMap.end() ? it->second : "application/octet-stream";
  }

public:
  MimeTypeDetector() {
#ifdef HAVE_LIBMAGIC
    magic_cookie = magic_open(MAGIC_MIME_TYPE);
    libmagic_available = (magic_cookie != nullptr);
    if (libmagic_available) {
      // Try to load magic database
#ifdef MAGIC_DB_PATH
      if (magic_load(magic_cookie, MAGIC_DB_PATH) != 0) {
#else
      if (magic_load(magic_cookie, nullptr) != 0) {
#endif
        magic_close(magic_cookie);
        libmagic_available = false;
      }
    }
#endif
  }

  ~MimeTypeDetector() {
#ifdef HAVE_LIBMAGIC
    if (libmagic_available && magic_cookie) {
      magic_close(magic_cookie);
    }
#endif
  }

  // Delete copy constructor and assignment operator
  MimeTypeDetector(const MimeTypeDetector &) = delete;
  MimeTypeDetector &operator=(const MimeTypeDetector &) = delete;

  std::string getMimeType(const std::string &filepath) {
#ifdef HAVE_LIBMAGIC
    if (libmagic_available) {
      const char *mime = magic_file(magic_cookie, filepath.c_str());
      if (mime) {
        return std::string(mime);
      }
    }
#endif
    // Fallback to extension-based detection
    return getMimeByExtension(filepath);
  }

  bool isUsingLibmagic() const {
#ifdef HAVE_LIBMAGIC
    return libmagic_available;
#else
    return false;
#endif
  }
};
