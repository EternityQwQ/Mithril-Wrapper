#include "abi/AbiManifest.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: mithril_abi_manifest_tool <manifest.json>\n";
        return 2;
    }
    std::ifstream stream(argv[1], std::ios::binary);
    if (!stream) {
        std::cerr << "cannot open manifest\n";
        return 2;
    }
    const std::string json{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    auto parsed = mithril::abi::AbiManifest::parse(json);
    if (!parsed) {
        std::cerr << parsed.error().text() << '\n';
        return 1;
    }
    std::cout << "format=" << parsed.value().formatVersion()
              << " symbols=" << parsed.value().symbols().size() << '\n';
    return 0;
}
