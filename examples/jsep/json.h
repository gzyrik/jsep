#ifndef __JSON_PARSER_H__
#define __JSON_PARSER_H__
#include <vector>
#include <string>
#include <sstream>
#include <cstdarg>
#include <cassert>
#include <map>
enum {
    /** 不带引号,具备原子性的JSON对象 */
    JSON_PRIMITIVE      = 1,
    /** 字符串 */
    JSON_STRING         = 2,
    /** 数组对象 */
    JSON_ARRAY          = 3,
    /** 复合的字典对象 */
    JSON_OBJECT         = 4,
};

enum {
    /** JSON对象表述符数组tokens, 长度不够 */
    JSON_ERROR_NOMEM    = -1,
    /** 非法的JSON字符串 */
    JSON_ERROR_INVAL    = -2,
    /** 字符串不是个完整的JSON格式 */
    JSON_ERROR_PART     = -3,
};

/**
 * JSON对象表述符.
 * @param[in] type 类型, 必须是JSON_OBJECT, JSON_ARRAY, JSON_STRING 之一
 * @param[in] start 在完整的JSON字符串的起始位置
 * @param[in] end 在完整的JSON字符串的结束位置
 */
typedef struct {
    int         type;
    int         start;
    int         end;
    int         size;
    int         parent;
} json_t;

/**
 * 解析器. 
 * 必须以0,初始化.
 */
typedef struct {
    /*< private >*/
    unsigned    _[3];
} json_p;

/**
 * 解析JSON字符串.
 * 数组tokens中将是依次每个JSON对象表述符.
 * JSON对象是指原子对象, 数组对象或复合对象.
 *
 * @param[in] parser 解析器
 * @param[in] json 完整的JSON字符串
 * @param[in] len 字符串json的有效长度
 * @param[out] tokens 解析后单个JSON对象表述符
 * @param[in] num_tokens 表述符数组tokens的有效个数
 * @return 成功返回0,反之返回JSON_ERROR_*值
 */
int json_parse(json_p *parser, const char *json, unsigned len, json_t *tokens, unsigned num_tokens);

/**
 * 类似strcmp,比较字符串.
 * 但是比较是基于JSON对象.
 * JSON对象是指原子对象, 数组对象或复合对象.
 *
 * @param[in] json 完整的JSON字符串
 * @param[in] t 指向单个JSON所在的位置
 * @param[in] key 比较的对象
 * @return 相等0, 大于1, 小于-1
 */
int json_strcmp(const char *json, json_t *token, const char *key);

/**
 * 跳过语法上单个JSON对象
 * JSON对象是指原子对象, 数组对象或复合对象.
 * 通常要跳过多个json_t.
 *
 * @param[in] t 当前json_t数组所在的位置
 * @param[in] end 当前json_t数组结束的位置
 * @return 返回路过的json_t数组个数
 */
int json_skip(const json_t *t, const json_t* end);

/** 检查字符串是否具备原子性 */
int json_atomic(const char* str);


struct json_o;
///JSON对象的字典
typedef std::map<std::string, json_o> json_map;
///JSON对象的数组
typedef std::vector<json_o>  json_vec;
/// 针对C++,封闭成更方便的对象
struct json_o
{
    static const json_o null;//非法的空JSON对象

    json_o():type(0),str(0){}
    json_o(const json_o& o):type(o.type), str(o.str) { o.type = 0; }
    ~json_o() { clean(); }
    ///从字符串构造JSON对象
    explicit json_o(const std::string& json):type(0),str(0) { if (!from(json)) clean(); }
    explicit json_o(const char *format, va_list argv):type(0),str(0) { if (!fromv(format, argv)) clean(); }
    explicit json_o(const char *format, ...):type(0),str(0) {
        va_list argv;
        va_start(argv, format);
        fromv(format, argv);
        va_end(argv);
    }
    ///从字符串生成JSON对象
    bool fromv(const char *format, va_list argv);
    bool from(const std::string& json);
    bool operator()(const char* format, ...) {
        va_list argv;
        va_start(argv, format);
        bool ret = fromv(format, argv);
        va_end(argv);
        return ret;
    }
    bool operator()(const std::string& json) { return from(json); }
    bool operator()(const char* format, va_list argv) { return fromv(format, argv); }

    ///将JSON对象还原成字符串
    std::string to_str() const;

    ///是否为简单JSON对象
    bool atomic() const {return (type == JSON_STRING || type == JSON_PRIMITIVE);}

    ///为逃逸字符加上前缀,返回符合JSON格式的字符串
    static std::string escape(const std::string& str);

    ///去掉JSON中逃逸字符的前缀,返回原字符串
    static std::string unescape(const std::string& json);

    ///检查字符串是否具备原子性
    static bool atomic(const std::string& str) { return json_atomic(str.c_str()) != 0; }

    mutable int type;
    union {
        std::string *str;
        json_map    *map;
        json_vec    *vec;
    };
    
    ///比较当前简单JSON对象与字符串是否相等
    bool operator ==(const char* s) const { return atomic() && *str == s;}
    bool operator ==(const std::string& s) const { return atomic() && *str == s; }
    bool operator !=(const std::string& s) const { return !(*this == s);}

    ///返回字典中元素,若不存在返回null
    const json_o& operator[](const std::string& key) const {
        if (type != JSON_OBJECT) return null;
        json_map::const_iterator iter = map->find(key);
        if (iter == map->end()) return null;
        return iter->second;
    }
    const json_o& operator[](const char* key) const { return (*this)[std::string(key)]; }

    ///返回字典中元素,若不存在则生成
    json_o& operator[](const std::string& key);
    json_o& operator[](const char* key) { return (*this)[std::string(key)]; }
    json_o& operator=(const json_o& o) { clean(); type = o.type; str = o.str; o.type = 0; return *this; }

    ///返回数组中元素,若不存在返回null
    const json_o& operator[](const int index) const {
        if (type != JSON_ARRAY) return null;
        if (index < 0 || index >= (int)vec->size()) return null;
        return (*vec)[index];
    }

    ///返回数组中元素,若不存在则生成
    json_o& operator[](const int index);

    ///判断是否为有效JOSN对象
    operator bool () const { return type != 0;}
    bool operator!() const { return type == 0;}

    ///返回简单JSON对象的字符串值
    operator std::string& () { assert(atomic()); return *str; }
    operator const std::string& () const{ assert(atomic()); return *str; }
    operator const char* () const { return atomic() ? str->c_str() : 0;}

    ///清空当前JSON对象,变成非法JSON对象
    void clean();
};
#endif /* __JSON_PARSER_H__ */
