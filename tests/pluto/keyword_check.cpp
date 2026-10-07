// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT
//
// Cross-check for the editor's Pluto keyword list (W7.2). The editor highlight
// and autocomplete data is hand-maintained in CustomTextEditor.cpp and drifts
// from Pluto's lexer whenever the pin moves; this fails the build when the two
// disagree, in either direction.
//
// Both paths are injected by tests/pluto/CMakeLists.txt, so the test always
// reads the fetched Pluto revision and the editor file from the same checkout.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#ifndef DORIAX_PLUTO_LLEX_PATH
#error "DORIAX_PLUTO_LLEX_PATH must point at the fetched Pluto llex.cpp"
#endif
#ifndef DORIAX_EDITOR_KEYWORDS_PATH
#error "DORIAX_EDITOR_KEYWORDS_PATH must point at editor/window/widget/CustomTextEditor.cpp"
#endif

namespace {

std::string readFile(const char* path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ok = false;
        return {};
    }
    ok = true;
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool isWord(const std::string& token) {
    if (token.empty()) return false;
    for (char c : token) {
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

// Pluto lists two parser-internal suggestions inside
// `#ifdef PLUTO_PARSER_SUGGESTIONS`. Their names carry digits, so they would
// stop the bare-word scan below; they are not language keywords and the editor
// does not highlight them, so drop the whole conditional block first.
std::string stripSuggestionsBlock(std::string text) {
    const std::string startMarker = "#ifdef PLUTO_PARSER_SUGGESTIONS";
    const std::string endMarker = "#endif";
    const size_t start = text.find(startMarker);
    if (start == std::string::npos) return text;

    const size_t end = text.find(endMarker, start);
    if (end == std::string::npos) return text;

    text.erase(start, end + endMarker.size() - start);
    return text;
}

// Quoted tokens inside the region that follows `marker`. The region ends at
// `endMarker` when one is given, and otherwise at the first quoted token that
// is not a bare word - which is where Pluto's table leaves the reserved words
// and starts listing operators.
std::set<std::string> reservedWords(const std::string& text, const std::string& marker,
                                    const std::string& endMarker) {
    std::set<std::string> words;
    const size_t markerPos = text.find(marker);
    if (markerPos == std::string::npos) return words;

    size_t pos = markerPos + marker.size();
    const size_t markerEnd = endMarker.empty() ? std::string::npos : text.find(endMarker, pos);
    const size_t regionEnd = (markerEnd == std::string::npos) ? text.size() : markerEnd;

    while (pos < regionEnd) {
        const size_t open = text.find('"', pos);
        if (open == std::string::npos || open >= regionEnd) break;
        const size_t close = text.find('"', open + 1);
        if (close == std::string::npos || close >= regionEnd) break;

        const std::string token = text.substr(open + 1, close - open - 1);
        if (!isWord(token)) break;
        words.insert(token);
        pos = close + 1;
    }
    return words;
}

} // namespace

int main() {
    bool ok = false;

    // Pluto's luaX_tokens: the reserved words run from the first entry until
    // the operator spellings ("//", "..", ...).
    const std::string llex = stripSuggestionsBlock(readFile(DORIAX_PLUTO_LLEX_PATH, ok));
    if (!ok) {
        std::printf("FAIL could not read %s\n", DORIAX_PLUTO_LLEX_PATH);
        return 1;
    }
    const std::set<std::string> plutoReserved = reservedWords(llex, "luaX_tokens", "");
    if (plutoReserved.empty()) {
        std::printf("FAIL could not find luaX_tokens in %s\n", DORIAX_PLUTO_LLEX_PATH);
        return 1;
    }

    // The editor's Lua grammar, which is what highlights .pluto files too.
    const std::string editor = readFile(DORIAX_EDITOR_KEYWORDS_PATH, ok);
    if (!ok) {
        std::printf("FAIL could not read %s\n", DORIAX_EDITOR_KEYWORDS_PATH);
        return 1;
    }
    const std::set<std::string> editorKeywords =
        reservedWords(editor, "case SyntaxLanguage::Lua:", "};");
    if (editorKeywords.empty()) {
        std::printf("FAIL could not find the Lua keyword list in %s\n", DORIAX_EDITOR_KEYWORDS_PATH);
        return 1;
    }

    int failures = 0;
    for (const std::string& word : plutoReserved) {
        if (editorKeywords.count(word) == 0) {
            std::printf("FAIL Pluto reserves '%s' but the editor does not highlight it\n", word.c_str());
            failures++;
        }
    }
    for (const std::string& word : editorKeywords) {
        if (plutoReserved.count(word) == 0) {
            std::printf("FAIL the editor highlights '%s' but Pluto does not reserve it\n", word.c_str());
            failures++;
        }
    }

    if (failures == 0) {
        std::printf("ok   editor keyword list matches Pluto (%zu words)\n", plutoReserved.size());
        std::printf("PLUTO_KEYWORDS_OK\n");
    }
    return failures == 0 ? 0 : 1;
}
