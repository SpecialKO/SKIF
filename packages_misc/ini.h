/**
 * Yet another .ini parser for modern c++ (made for cpp17),
 * Project page: https://github.com/SSARCandy/ini-cpp
 */

#ifndef INI_CPP_INI_H_
#define INI_CPP_INI_H_

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <codecvt>

#if defined(__has_include)
#if __has_include(<charconv>)
#include <charconv>
#define INI_CPP_HAS_CHARCONV 1
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#define INI_CPP_HAS_FLOAT_CHARCONV 1
#endif
#endif
#endif

namespace inih {

namespace detail {

/* Locale-independent whitespace check (same set as C isspace). */
inline constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

inline std::string_view ltrim(std::string_view s) noexcept {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    return s;
}

inline std::string_view rtrim(std::string_view s) noexcept {
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

inline std::string_view trim(std::string_view s) noexcept {
    return ltrim(rtrim(s));
}

/* Return index of the first char of `chars` or of an inline comment (a ';'
   preceded by whitespace) in `s`, or npos if neither is found. */
inline std::size_t find_char_or_comment(std::string_view s,
                                        std::string_view chars) noexcept {
    bool was_space = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (chars.find(c) != std::string_view::npos ||
            (was_space && c == ';')) {
            return i;
        }
        was_space = is_space(c);
    }
    return std::string_view::npos;
}

/* True for types that can be parsed with std::from_chars. Character types
   are excluded so they keep stream semantics ("7" -> '7', not int 7). */
template <typename T>
inline constexpr bool use_charconv =
#if defined(INI_CPP_HAS_FLOAT_CHARCONV)
    std::is_floating_point_v<T> ||
#endif
    (std::is_integral_v<T> && !std::is_same_v<T, bool> &&
     !std::is_same_v<T, char> && !std::is_same_v<T, signed char> &&
     !std::is_same_v<T, unsigned char> && !std::is_same_v<T, wchar_t> &&
     !std::is_same_v<T, char16_t> && !std::is_same_v<T, char32_t>);

/* Parse `s` into `v`, return false on failure. Uses std::from_chars for
   numbers when available, stream extraction otherwise. */
template <typename T>
inline bool parse_value(const std::string& s, T& v) {
#if defined(INI_CPP_HAS_CHARCONV)
    if constexpr (use_charconv<T>) {
        const char* first = s.data();
        const char* const last = s.data() + s.size();
        while (first != last && is_space(*first)) ++first;
        // istream compatibility: allow an explicit leading '+'
        if (last - first > 1 && *first == '+' &&
            ((first[1] >= '0' && first[1] <= '9') || first[1] == '.')) {
            ++first;
        }
        return std::from_chars(first, last, v).ec == std::errc{};
    } else
#endif
    {
        std::istringstream in{s};
        in >> v;
        return !in.fail();
    }
}

}  // namespace detail

/**
 * @brief Read an INI file into easy-to-access name/value pairs.
 */
class INIReader {
   public:
    // Empty Constructor
    INIReader() = default;

    /**
     * @brief Construct an INIReader object from a file name
     * @param filename The name of the INI file to parse
     * @throws std::runtime_error if there is an error parsing the INI file
     */
    INIReader(const std::string& filename) {
        std::ifstream in{filename, std::ios::in | std::ios::binary};
        if (!in) {
            _error = -1;
            ParseError();
            return;
        }
        std::string content;
        in.seekg(0, std::ios::end);
        const auto size = in.tellg();
        if (size > 0) {
            content.resize(static_cast<std::size_t>(size));
            in.seekg(0, std::ios::beg);
            in.read(&content[0], size);
        }
        Parse(content);
        ParseError();
    }

    /**
     * @brief Construct an INIReader object from a file pointer
     * @param file A pointer to the INI file to parse
     * @throws std::runtime_error if there is an error parsing the INI file
     */
    INIReader(std::FILE* file) {
        std::string content;
        char buf[1 << 15];
        std::size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), file)) > 0) {
            content.append(buf, n);
        }
        Parse(content);
        ParseError();
    }

    /**
     * @brief Construct an INIReader object from a std::string
     * @param content The variable containing the INI object to be parsed
     * @throws std::runtime_error if there is an error parsing the INI object
     */
    void ParseContent(const std::string& content) {
        Parse(content);
        ParseError();
    }

    /**
     * @brief Return the result of the parse, i.e., 0 on success
     * @throws std::runtime_error on file open or parse error
     */
    int ParseError() const {
        switch (_error) {
            case 0:
                break;
            case -1:
                throw std::runtime_error("ini file not found.");
            case -2:
                throw std::runtime_error("memory alloc error");
            default:
                throw std::runtime_error("parse error on line no: " +
                                         std::to_string(_error));
        }
        return 0;
    }

    /**
     * @brief Return the list of sections found in ini file
     * @return The list of sections found in ini file
     */
    std::set<std::string> Sections() const {
        std::set<std::string> retval;
        for (const auto& element : _values) {
            retval.insert(element.first);
        }
        return retval;
    }

    /**
     * @brief Return the list of keys in the given section
     * @param section The section name
     * @return The list of keys in the given section
     */
    std::set<std::string> Keys(const std::string& section) const {
        const auto& sec = GetSection(section);
        std::set<std::string> retval;
        for (const auto& element : sec) {
            retval.insert(element.first);
        }
        return retval;
    }

    /**
     * @brief Get the map representing the values in a section of the INI file
     * @param section The name of the section to retrieve
     * @return The map representing the values in the given section
     * @throws std::runtime_error if the section is not found
     */
    std::unordered_map<std::string, std::string> Get(
        const std::string& section) const {
        return GetSection(section);
    }

    /**
     * @brief Return the value of the given key in the given section
     * @param section The section name
     * @param name The key name
     * @return The value of the given key in the given section
     * @throws std::runtime_error if the section/key is not found or the
     * value cannot be parsed to type T
     */
    template <typename T = std::string>
    T Get(const std::string& section, const std::string& name) const {
        const auto& sec = GetSection(section);
        const auto value = sec.find(name);
        if (value == sec.end()) {
            throw std::runtime_error(
                "key '" + name + "' not found in section '" + section + "'.");
        }

        if constexpr (std::is_same_v<T, std::string>) {
            return value->second;
        } else if constexpr (std::is_same_v<T, bool>) {
            return BoolConverter(value->second);
        } else {
            return Converter<T>(value->second);
        }
    }

    /**
     * @brief Return the value of the given key in the given section, return
     * default if not found
     * @param section The section name
     * @param name The key name
     * @param default_v The default value
     * @return The value of the given key in the given section, return default
     * if not found
     */
    template <typename T>
    T Get(const std::string& section, const std::string& name,
          T&& default_v) const {
        try {
            return Get<T>(section, name);
        } catch (std::runtime_error&) {
            return std::forward<T>(default_v);
        }
    }

    /**
     * @brief Return the value array of the given key in the given section.
     * @param section The section name
     * @param name The key name
     * @return The value array of the given key in the given section.
     *
     * For example:
     * ```ini
     * [section]
     * key = 1 2 3 4
     * ```
     * ```cpp
     * const auto vs = ini.GetVector<int>("section", "key");
     * // vs = {1, 2, 3, 4}
     * ```
     */
    template <typename T = std::string>
    std::vector<T> GetVector(const std::string& section,
                             const std::string& name) const {
        const std::string value = Get(section, name);
        try {
            std::vector<T> vs;
            std::size_t i = 0;
            while (i < value.size()) {
                while (i < value.size() && detail::is_space(value[i])) ++i;
                std::size_t j = i;
                while (j < value.size() && !detail::is_space(value[j])) ++j;
                if (j > i)
                    vs.emplace_back(Converter<T>(value.substr(i, j - i)));
                i = j;
            }
            return vs;
        } catch (std::exception&) {
            throw std::runtime_error("cannot parse value " + value +
                                     " to vector<T>.");
        }
    }

    /**
     * @brief Return the value array of the given key in the given section,
     * return default if not found
     * @param section The section name
     * @param name The key name
     * @param default_v The default value
     * @return The value array of the given key in the given section, return
     * default if not found
     *
     * @see INIReader::GetVector
     */
    template <typename T>
    std::vector<T> GetVector(const std::string& section,
                             const std::string& name,
                             const std::vector<T>& default_v) const {
        try {
            return GetVector<T>(section, name);
        } catch (std::runtime_error&) {
            return default_v;
        }
    }

    /**
     * @brief Insert a key-value pair into the INI file
     * @param section The section name
     * @param name The key name
     * @param v The value to insert
     * @throws std::runtime_error if the key already exists in the section
     */
    template <typename T = std::string>
    void InsertEntry(const std::string& section, const std::string& name,
                     const T& v) {
        if (!_values[section].emplace(name, V2String(v)).second) {
            throw std::runtime_error("duplicate key '" + name +
                                     "' in section '" + section + "'.");
        }
    }

    /**
     * @brief Insert a vector of values into the INI file
     * @param section The section name
     * @param name The key name
     * @param vs The vector of values to insert
     * @throws std::runtime_error if the key already exists in the section
     */
    template <typename T = std::string>
    void InsertEntry(const std::string& section, const std::string& name,
                     const std::vector<T>& vs) {
        if (!_values[section].emplace(name, Vec2String(vs)).second) {
            throw std::runtime_error("duplicate key '" + name +
                                     "' in section '" + section + "'.");
        }
    }

    /**
     * @brief Update a key-value pair in the INI file
     * @param section The section name
     * @param name The key name
     * @param v The new value to set
     * @throws std::runtime_error if the key does not exist in the section
     */
    template <typename T = std::string>
    void UpdateEntry(const std::string& section, const std::string& name,
                     const T& v) {
        FindEntry(section, name) = V2String(v);
    }

    /**
     * @brief Update a vector of values in the INI file
     * @param section The section name
     * @param name The key name
     * @param vs The new vector of values to set
     * @throws std::runtime_error if the key does not exist in the section
     */
    template <typename T = std::string>
    void UpdateEntry(const std::string& section, const std::string& name,
                     const std::vector<T>& vs) {
        FindEntry(section, name) = Vec2String(vs);
    }

   protected:
    /// Parse result: 0 on success, -1 on file open error, otherwise the
    /// number of the first faulty line.
    int _error = 0;
    /// Parsed content, as _values[section][name] = value.
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>>
        _values;

    /// Parse `s` as a `T`; throws std::runtime_error on failure.
    template <typename T>
    T Converter(const std::string& s) const {
        if constexpr (std::is_same_v<T, std::string>) {
            return s;
        } else {
            T v{};
            if (!detail::parse_value(s, v)) {
                throw std::runtime_error("cannot parse value '" + s +
                                         "' to type<T>.");
            }
            return v;
        }
    }

    /// Parse a boolean token: 1/0/true/false/yes/no/on/off, case-insensitive;
    /// throws std::runtime_error on anything else.
    bool BoolConverter(std::string s) const {
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        }
        static const std::unordered_map<std::string, bool> s2b{
            {"1", true},  {"true", true},   {"yes", true}, {"on", true},
            {"0", false}, {"false", false}, {"no", false}, {"off", false},
        };
        const auto value = s2b.find(s);
        if (value == s2b.end()) {
            throw std::runtime_error("'" + s +
                                     "' is not a valid boolean value.");
        }
        return value->second;
    }

    /// Serialize a value with operator<<.
    template <typename T>
    std::string V2String(const T& v) const {
        std::ostringstream ss;
        ss << v;
        return ss.str();
    }

    /// Serialize a vector as space-separated values.
    template <typename T>
    std::string Vec2String(const std::vector<T>& v) const {
        std::ostringstream oss;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i > 0) oss << ' ';
            oss << v[i];
        }
        return oss.str();
    }

   private:
    const std::unordered_map<std::string, std::string>& GetSection(
        const std::string& section) const {
        const auto sec = _values.find(section);
        if (sec == _values.end()) {
            throw std::runtime_error("section '" + section + "' not found.");
        }
        return sec->second;
    }

    std::string& FindEntry(const std::string& section,
                           const std::string& name) {
        const auto sec = _values.find(section);
        if (sec != _values.end()) {
            const auto value = sec->second.find(name);
            if (value != sec->second.end()) {
                return value->second;
            }
        }
        throw std::runtime_error("key '" + name + "' not exist in section '" +
                                 section + "'.");
    }

    /* Parse the whole ini content. Grammar:
       - `[section]` lines open a section; text after ']' is ignored
       - `name = value` or `name : value` pairs, whitespace-trimmed
       - lines starting with ';' or '#' are comments
       - a ';' preceded by whitespace starts an inline comment
       Records the first faulty line in _error and stops there. Throws on
       duplicate keys. */
    void Parse(std::string_view content) {
        constexpr std::string_view bom{"\xEF\xBB\xBF", 3};
        if (content.substr(0, bom.size()) == bom) {
            content.remove_prefix(bom.size());
        }

        std::string section;
        std::unordered_map<std::string, std::string>* values = nullptr;
        int lineno = 0;
        _error = 0;

        while (!content.empty()) {
            ++lineno;
            const auto eol = content.find('\n');
            const auto line = detail::trim(content.substr(0, eol));
            content.remove_prefix(eol == std::string_view::npos ? content.size()
                                                                : eol + 1);

            if (line.empty() || line.front() == ';' || line.front() == '#') {
                /* Blank line or comment */
            } else if (line.front() == '[') {
                /* A "[section]" line */
                const auto end =
                    detail::find_char_or_comment(line.substr(1), "]");
                if (end != std::string_view::npos && line[end + 1] == ']') {
                    section.assign(line.data() + 1, end);
                    values = nullptr;
                } else {
                    /* No ']' found on section line */
                    _error = lineno;
                    break;
                }
            } else {
                /* Not a comment, must be a name[=:]value pair */
                const auto sep = detail::find_char_or_comment(line, "="); // Special K does not use ':' for key-value pairs
                if (sep == std::string_view::npos || line[sep] == ';') {
                    /* No '=' or ':' found on name[=:]value line */
                    _error = lineno;
                    break;
                }
                const auto name = detail::rtrim(line.substr(0, sep));
                auto value = line.substr(sep + 1);
                const auto comment = detail::find_char_or_comment(value, {});
                if (comment != std::string_view::npos) {
                    value = value.substr(0, comment);
                }
                value = detail::trim(value);

                if (values == nullptr) {
                    values = &_values[section];
                }
                if (!values->emplace(std::string(name), std::string(value))
                         .second) {
                    throw std::runtime_error("duplicate key '" +
                                             std::string(name) +
                                             "' in section '" + section + "'.");
                }
            }
        }
    }
};

/**
 * @brief Write the contents of an INIReader to an ini file.
 */
class INIWriter {
   public:
    INIWriter() = default;
    /**
     * @brief Write the contents of an INI file to a new file
     * @param filepath The path of the output file
     * @param reader The INIReader object to write to the file
     * @param overwrite Whether to just overwrite an existing file
     * @throws std::runtime_error if the output file already exists or cannot
     * be opened
     */
    inline static void write(const std::string& filepath,
                             const INIReader& reader,
                             const bool overwrite = false) {
        if (!overwrite && std::ifstream{filepath}) {
            throw std::runtime_error("file: " + filepath + " already exists.");
        }
        std::ofstream out{filepath};
        if (!out.is_open()) {
            throw std::runtime_error("cannot open output file: " + filepath);
        }
        for (const auto& section : reader.Sections()) {
            out << "[" << section << "]\n";
            for (const auto& key : reader.Keys(section)) {
                out << key << "=" << reader.Get(section, key) << "\n";
            }
            out << "\n";
        }
    }
};

}  // namespace inih

#endif  // INI_CPP_INI_H_
