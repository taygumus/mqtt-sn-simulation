#include "StringHelper.h"
#include <algorithm>

namespace mqttsn {

const std::string StringHelper::base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string StringHelper::base64Encode(const std::string& inputString)
{
    int val = 0, valb = -6;
    std::string encodedString;

    for (unsigned char c : inputString) {
        val = (val << 8) + c;
        valb += 8;

        while (valb >= 0) {
            encodedString.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }

    if (valb > -6) {
        encodedString.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }

    while (encodedString.size() % 4) {
        encodedString.push_back('=');
    }

    return encodedString;
}

std::string StringHelper::base64Decode(const std::string& inputString) {
    std::string decodedString;
    int val = 0, valb = -8;

    for (unsigned char c : inputString) {
        // ignore whitespace characters
        if (std::isspace(c)) continue;

        // invalid character in Base64 string
        if (!(std::isalnum(c) || (c == '+') || (c == '/'))) return "";

        val = (val << 6) + base64_chars.find(c);
        valb += 6;

        if (valb >= 0) {
            decodedString.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return decodedString;
}

std::string StringHelper::appendCounterToString(const std::string& inputString, int counter)
{
    if (counter == 0) {
        return inputString;
    }

    return inputString + std::to_string(counter);
}

std::string StringHelper::sanitizeSpaces(const std::string& inputString)
{
    std::string sanitizedString = inputString;
    sanitizedString.erase(std::remove_if(sanitizedString.begin(), sanitizedString.end(), ::isspace), sanitizedString.end());

    return sanitizedString;
}

} /* namespace mqttsn */
