// No-op stand-in for the Slic3r::Http symbols that libslic3r's LogSink.cpp
// references across the library boundary (the real implementation lives in
// the GUI library, which this lean test target must not link). Only the
// members LogSink uses are defined; nothing here performs any I/O.
#include "slic3r/Utils/BBLUtil.hpp"
#include "slic3r/Utils/Http.hpp"

namespace Slic3r {

bool BBL_Encrypt::AES256CBC_Encrypt(unsigned char *, unsigned, unsigned char *,
                                    unsigned &out_len, const std::string &, const std::string &)
{
    out_len = 0;
    return false;
}

bool BBL_Encrypt::AES256CBC_Decrypt(unsigned char *, unsigned, unsigned char *,
                                    unsigned &out_len, const std::string &, const std::string &)
{
    out_len = 0;
    return false;
}

struct Http::priv {};

Http::Http(const std::string & /* url */) {}
Http::Http(Http &&other) : p(std::move(other.p)) {}
Http::~Http() {}

Http Http::get(std::string /* url */) { return Http(std::string()); }

Http &Http::on_complete(CompleteFn /* fn */) { return *this; }
Http &Http::on_error(ErrorFn /* fn */) { return *this; }
Http &Http::timeout_max(long /* timeout */) { return *this; }

void Http::perform_sync() {}

} // namespace Slic3r
