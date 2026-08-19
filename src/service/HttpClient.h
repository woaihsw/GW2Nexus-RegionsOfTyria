#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H
#pragma comment(lib, "urlmon.lib")

#include <urlmon.h>
#include <string>
#include <sstream>
#include <format>

namespace HTTPClient {
    std::string GetRequest(std::string url) {
        const char* cUrl = url.c_str();
        std::wstring wUrl(cUrl, cUrl + strlen(cUrl));
        IStream* stream;
        HRESULT result = URLOpenBlockingStream(0, wUrl.c_str(), &stream, 0, 0);
        if (result != 0) {
            return "";
        }

        const unsigned long chunkSize = 65536;
        char buffer[chunkSize];
        unsigned long bytesRead;
        std::stringstream strStream;

        stream->Read(buffer, chunkSize, &bytesRead);
        while (bytesRead > 0) {
            strStream.write(buffer, (long long)bytesRead);
            stream->Read(buffer, chunkSize, &bytesRead);
        }
        stream->Release();
        std::string response = strStream.str();
        return response;
    }
}
#endif