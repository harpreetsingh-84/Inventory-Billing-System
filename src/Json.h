#ifndef JSON_H
#define JSON_H
// Tiny JSON helper: parser for request bodies + string escaper for responses.
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <cctype>
#include <cstdio>

struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
    double n = 0;
    bool b = false;
    std::string s;
    std::vector<JVal> a;                 // array items
    std::vector<std::string> keys;       // object keys
    std::vector<JVal> vals;              // object values
    const JVal* get(const std::string& k) const {
        for (size_t i = 0; i < keys.size(); i++) if (keys[i] == k) return &vals[i];
        return nullptr;
    }
};

inline std::string jstr(const std::string& in) {
    std::string r = "\"";
    for (unsigned char c : in) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); r += b; }
                else r += (char)c;
        }
    }
    return r + "\"";
}

class JParser {
    const std::string& s;
    size_t i = 0;
    [[noreturn]] void fail() { throw std::invalid_argument("Invalid JSON"); }
    void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) i++; }
    std::string str() {
        std::string r; i++;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                switch (e) {
                    case 'n': r += '\n'; break; case 't': r += '\t'; break;
                    case 'r': r += '\r'; break; case 'b': r += '\b'; break;
                    case 'f': r += '\f'; break;
                    case 'u': {
                        if (i + 4 > s.size()) fail();
                        unsigned cp = (unsigned)std::stoul(s.substr(i, 4), nullptr, 16); i += 4;
                        if (cp < 0x80) r += (char)cp;
                        else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3F)); }
                        else { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3F)); r += (char)(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: r += e;
                }
            } else r += c;
        }
        if (i >= s.size()) fail();
        i++;
        return r;
    }
    JVal val(int depth) {
        if (depth > 32) fail();
        ws();
        if (i >= s.size()) fail();
        JVal v;
        char c = s[i];
        if (c == '{') {
            v.t = JVal::Obj; i++; ws();
            if (i < s.size() && s[i] == '}') { i++; return v; }
            while (true) {
                ws(); if (i >= s.size() || s[i] != '"') fail();
                std::string k = str(); ws();
                if (i >= s.size() || s[i] != ':') fail();
                i++;
                v.keys.push_back(k); v.vals.push_back(val(depth + 1)); ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; break; }
                fail();
            }
        } else if (c == '[') {
            v.t = JVal::Arr; i++; ws();
            if (i < s.size() && s[i] == ']') { i++; return v; }
            while (true) {
                v.a.push_back(val(depth + 1)); ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; break; }
                fail();
            }
        } else if (c == '"') { v.t = JVal::Str; v.s = str(); }
        else if (s.compare(i, 4, "true") == 0) { v.t = JVal::Bool; v.b = true; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { v.t = JVal::Bool; i += 5; }
        else if (s.compare(i, 4, "null") == 0) { i += 4; }
        else {
            char* end; v.n = std::strtod(s.c_str() + i, &end);
            if (end == s.c_str() + i) fail();
            v.t = JVal::Num; i = end - s.c_str();
        }
        return v;
    }
public:
    explicit JParser(const std::string& str_) : s(str_) {}
    JVal parse() { JVal v = val(0); ws(); if (i != s.size()) fail(); return v; }
};

inline JVal parseJson(const std::string& s) { return JParser(s).parse(); }
#endif
