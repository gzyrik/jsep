#include "json.h"
#include <string>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#define JSON_PARENT_LINKS
#ifndef NULL
#define NULL (void*)0
#endif
/**
 * JSON parser. Contains an array of token blocks available. Also stores
 * the string being parsed now and current position in that string
 */
typedef struct {
    /*< private >*/
    unsigned int pos; /* offset in the JSON string */
    unsigned int toknext; /* next token to allocate */
    unsigned int toksuper1; /* superior token node, e.g parent object or array, start from 1, 0 is no parent */
} json_parser;

/**
 * Allocates a fresh unused token from the token pull.
 */
static json_t *json_alloc_token(json_parser *parser,
                                json_t *tokens, unsigned num_tokens)
{
    json_t *tok;
    if (parser->toknext >= num_tokens) {
        return NULL;
    }
    tok = &tokens[parser->toknext++];
    tok->start = tok->end = -1;
    tok->size = 0;
#ifdef JSON_PARENT_LINKS
    tok->parent = -1;
#endif
    return tok;
}

/**
 * Fills token type and boundaries.
 */
static void json_fill_token(json_t *token, int type,
                            int start, int end)
{
    token->type = type;
    token->start = start;
    token->end = end;
    token->size = 0;
}

/**
 * Fills next available token with JSON primitive.
 */
static int json_parse_primitive(json_parser *parser, const char *js,
                                unsigned len, json_t *tokens, unsigned num_tokens)
{
    json_t *token;
    int start;

    start = parser->pos;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
        switch (js[parser->pos]) {
#ifndef JSON_STRICT
            /* In strict mode primitive must be followed by "," or "}" or "]" */
        case ':':
#endif
        case '\t' : case '\r' : case '\n' : case ' ' :
        case ','  : case ']'  : case '}' :
            goto found;
        }
        if (js[parser->pos] < 32 || js[parser->pos] >= 127) {
            parser->pos = start;
            return JSON_ERROR_INVAL;
        }
    }
#ifdef JSON_STRICT
    /* In strict mode primitive must be followed by a comma/object/array */
    parser->pos = start;
    return JSON_ERROR_PART;
#endif

found:
    if (tokens == NULL) {
        parser->pos--;
        return 0;
    }
    token = json_alloc_token(parser, tokens, num_tokens);
    if (token == NULL) {
        parser->pos = start;
        return JSON_ERROR_NOMEM;
    }
    json_fill_token(token, JSON_PRIMITIVE, start, parser->pos);
#ifdef JSON_PARENT_LINKS
    token->parent = parser->toksuper1-1;
#endif
    parser->pos--;
    return 0;
}

/**
 * Filsl next token with JSON string.
 */
static int json_parse_string(json_parser *parser, const char *js,
                             unsigned len, json_t *tokens, unsigned num_tokens)
{
    json_t *token;

    int start = parser->pos;

    parser->pos++;

    /* Skip starting quote */
    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
        char c = js[parser->pos];

        /* Quote: end of string */
        if (c == '\"') {
            if (tokens == NULL) {
                return 0;
            }
            token = json_alloc_token(parser, tokens, num_tokens);
            if (token == NULL) {
                parser->pos = start;
                return JSON_ERROR_NOMEM;
            }
            json_fill_token(token, JSON_STRING, start+1, parser->pos);
#ifdef JSON_PARENT_LINKS
            token->parent = parser->toksuper1-1;
#endif
            return 0;
        }

        /* Backslash: Quoted symbol expected */
        if (c == '\\' && parser->pos + 1 < len) {
            int i;
            parser->pos++;
            switch (js[parser->pos]) {
                /* Allowed escaped symbols */
            case '\"': case '/' : case '\\' : case 'b' :
            case 'f' : case 'r' : case 'n'  : case 't' :
                break;
                /* Allows escaped symbol \uXXXX */
            case 'u':
                parser->pos++;
                for(i = 0; i < 4 && parser->pos < len && js[parser->pos] != '\0'; i++) {
                    /* If it isn't a hex character we have an error */
                    if(!((js[parser->pos] >= 48 && js[parser->pos] <= 57) || /* 0-9 */
                         (js[parser->pos] >= 65 && js[parser->pos] <= 70) || /* A-F */
                         (js[parser->pos] >= 97 && js[parser->pos] <= 102))) { /* a-f */
                        parser->pos = start;
                        return JSON_ERROR_INVAL;
                    }
                    parser->pos++;
                }
                parser->pos--;
                break;
                /* Unexpected symbol */
            default:
                parser->pos = start;
                return JSON_ERROR_INVAL;
            }
        }
    }
    parser->pos = start;
    return JSON_ERROR_PART;
}

/**
 * Parse JSON string and fill tokens.
 */
int json_parse(json_p *p, const char *js, unsigned len,
               json_t *tokens, unsigned num_tokens) {
    int r;
    int i;
    json_t *token;
    json_parser *parser = (json_parser*)p;
    int count = parser->toknext;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
        char c;
        int type;

        c = js[parser->pos];
        switch (c) {
        case '{': case '[':
            count++;
            if (tokens == NULL) {
                break;
            }
            token = json_alloc_token(parser, tokens, num_tokens);
            if (token == NULL)
                return JSON_ERROR_NOMEM;
            if (parser->toksuper1 != 0) {
                tokens[parser->toksuper1-1].size++;
#ifdef JSON_PARENT_LINKS
                token->parent = parser->toksuper1-1;
#endif
            }
            token->type = (c == '{' ? JSON_OBJECT : JSON_ARRAY);
            token->start = parser->pos;
            parser->toksuper1 = parser->toknext;
            break;
        case '}': case ']':
            if (tokens == NULL)
                break;
            type = (c == '}' ? JSON_OBJECT : JSON_ARRAY);
#ifdef JSON_PARENT_LINKS
            if (parser->toknext < 1) {
                return JSON_ERROR_INVAL;
            }
            token = &tokens[parser->toknext - 1];
            for (;;) {
                if (token->start != -1 && token->end == -1) {
                    if (token->type != type) {
                        return JSON_ERROR_INVAL;
                    }
                    token->end = parser->pos + 1;
                    parser->toksuper1 = token->parent+1;
                    break;
                }
                if (token->parent == -1) {
                    break;
                }
                token = &tokens[token->parent];
            }
#else
            for (i = parser->toknext - 1; i >= 0; i--) {
                token = &tokens[i];
                if (token->start != -1 && token->end == -1) {
                    if (token->type != type) {
                        return JSON_ERROR_INVAL;
                    }
                    parser->toksuper1 = 0;
                    token->end = parser->pos + 1;
                    break;
                }
            }
            /* Error if unmatched closing bracket */
            if (i == -1) return JSON_ERROR_INVAL;
            for (; i >= 0; i--) {
                token = &tokens[i];
                if (token->start != -1 && token->end == -1) {
                    parser->toksuper1 = i+1;
                    break;
                }
            }
#endif
            break;
        case '\"':
            r = json_parse_string(parser, js, len, tokens, num_tokens);
            if (r < 0) return r;
            count++;
            if (parser->toksuper1 != 0 && tokens != NULL)
                tokens[parser->toksuper1-1].size++;
            break;
        case '\t' : case '\r' : case '\n' : case ' ':
            break;
        case ':':
            parser->toksuper1 = parser->toknext;
            break;
        case ',':
            if (tokens != NULL && parser->toksuper1 &&
                tokens[parser->toksuper1-1].type != JSON_ARRAY &&
                tokens[parser->toksuper1-1].type != JSON_OBJECT) {
#ifdef JSON_PARENT_LINKS
                parser->toksuper1 = tokens[parser->toksuper1-1].parent+1;
#else
                for (i = parser->toknext - 1; i >= 0; i--) {
                    if (tokens[i].type == JSON_ARRAY || tokens[i].type == JSON_OBJECT) {
                        if (tokens[i].start != -1 && tokens[i].end == -1) {
                            parser->toksuper1 = i+1;
                            break;
                        }
                    }
                }
#endif
            }
            break;
#ifdef JSON_STRICT
            /* In strict mode primitives are: numbers and booleans */
        case '-': case '0': case '1' : case '2': case '3' : case '4':
        case '5': case '6': case '7' : case '8': case '9':
        case 't': case 'f': case 'n' :
            /* And they must not be keys of the object */
            if (tokens != NULL) {
                json_t *t = &tokens[parser->toksuper1-1];
                if (t->type == JSON_OBJECT ||
                    (t->type == JSON_STRING && t->size != 0)) {
                    return JSON_ERROR_INVAL;
                }
            }
#else
            /* In non-strict mode every unquoted value is a primitive */
        default:
#endif
            r = json_parse_primitive(parser, js, len, tokens, num_tokens);
            if (r < 0) return r;
            count++;
            if (parser->toksuper1 != 0 && tokens != NULL)
                tokens[parser->toksuper1-1].size++;
            break;

#ifdef JSON_STRICT
            /* Unexpected char in strict mode */
        default:
            return JSON_ERROR_INVAL;
#endif
        }
    }

    for (i = parser->toknext - 1; i >= 0; i--) {
        /* Unmatched opened object or array */
        if (tokens[i].start != -1 && tokens[i].end == -1) {
            return JSON_ERROR_PART;
        }
    }
    if(tokens && count != (int)parser->toknext)
        return JSON_ERROR_INVAL;
    return count;
}

int json_strcmp(const char *json, json_t *tok, const char *s)
{
    int i;
    i = tok->start;
    while (i<tok->end && *s && json[i] == *s)
        ++i, ++s;
    return i>=tok->end ? 0 - *s : json[i] - *s;
}
int json_skip(const json_t *t, const json_t* end)
{
    int i;
    const json_t* tag = t;
    const int size = t->size;
    const int type = t->type;
    ++t;
    for (i=0;i<size && t < end;++i) {
        if (type == JSON_OBJECT)
            ++t;
        t += json_skip(t,end);
    }
    return t - tag;
}
int json_atomic(const char* str)
{
    if (!str) return false;
    while (1) {
        const unsigned char c = (unsigned char)*str++;
        switch(c){
        case '\\':
        case '\"':
        case '/':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
        case ' ': return false;
        case '\0': return true;
        default: if (c&0x80) return false;
        }
    }
    return true;
}

static const json_t* get_obj(json_o& obj, const char *json, const json_t *t, const json_t* const end)
{
    int i;
    const int type = t->type;
    const int size = t->size;
    switch(type) {
    case JSON_OBJECT:
        ++t;
        obj.map = new std::map<std::string, json_o>;
        obj.type = JSON_OBJECT;
        for(i=0;i<size && t < end;++i)
            t = get_obj((*obj.map)[std::string(json+t->start, t->end-t->start)], json, t+1, end);
        break;
    case JSON_ARRAY:
        ++t;
        obj.vec = new std::vector<json_o>(size);
        obj.type = JSON_ARRAY;
        for(i=0;i<size && t < end;++i)
            t = get_obj((*obj.vec)[i], json, t, end);
        if (i != size) obj.vec->resize(i);
        break;
    case JSON_STRING:
    case JSON_PRIMITIVE:
        obj.str = new std::string(json+t->start, t->end-t->start);
        obj.type = type;
        ++t;
        break;
    default:
        return end;
    }
    return t;
}
static bool json_obj(json_o& obj, const char *json, unsigned len)
{
    json_t token[256];
    int r, n= sizeof(token)/sizeof(json_t);
    json_t *t = token;
    obj.clean();
    {
        json_p parse;
        memset(&parse, 0, sizeof(parse));
        do {
            r = json_parse(&parse, json, len, t, n);
            if (r == JSON_ERROR_NOMEM) {
                n *= 2;
                if (t == token)
                    t = (json_t*)memcpy(malloc(sizeof(json_t)*n), token, sizeof(token));
                else
                    t = (json_t *)realloc(t, sizeof(json_t)*n);
            }
            else if (r > 0)
                break;
            else {
                if (t != token) {
                    free(t);
                    t = NULL;
                }
                return false;
            }
        } while(1);
    }

    if (t->type != JSON_OBJECT && r > 1 && t->size <= 1) {
        const json_t* it = t;
        const json_t* const end = t + r;
        const char* pColon = strchr(json, ':');
        const char* pComma = strchr(json, ',');
        if (!pColon) pColon = json+len;
        if (!pComma) pComma = json+len;
        if (pColon < pComma) {
            obj.map = new std::map<std::string, json_o>;
            obj.type = JSON_OBJECT;
            while(it< end)
                it = get_obj((*obj.map)[std::string(json+it->start, it->end-it->start)], json, it+1, end);
        }
        else {
            int i=0;
            obj.vec = new std::vector<json_o>(r);
            obj.type = JSON_ARRAY;
            while(it < end) {
                it = get_obj((*obj.vec)[i], json, it, end);
                ++i;
            }
            if (i != r) obj.vec->resize(i);
        }
    }
    else  {
        get_obj(obj, json, t, r+t);
    }
    if (t != token) {
        free(t);
        t = NULL;
    }
    return true;
}
static bool json_obj(json_o& obj, const char *format, va_list argv)
{
    char json[8192];
    int size = sizeof(json);
    char* j = json;
    int try_count = 0;
try_printf:
    int len = vsnprintf(j, size, format, argv);
    if (try_count < 2 && (len >= size || len < 0)) {
        if (j == json) j = NULL;
        size *= 2;
        j = (char*)realloc(j, size);
        try_count++;
        goto try_printf;
    }
    bool ret = json_obj(obj, j, len);
    if (j != json) {
        free(j);
        j = NULL;
    }
    return ret;
}
static void pub_obj(const json_o& obj, std::ostream& oss)
{
    bool first = true;
    switch(obj.type) {
    case JSON_OBJECT:
        oss<<'{';
        for(std::map<std::string, json_o>::const_iterator iter = obj.map->begin();
            iter != obj.map->end(); ++iter) {
            if (!iter->second.type) continue;
            if (!first) oss<<',';
            pub_obj(iter->second, oss<<'\"'<<iter->first<<"\":");
            first = false;
        }
        oss<<'}';
        break;
    case JSON_ARRAY:
        oss<<'[';
        for(std::vector<json_o>::const_iterator iter = obj.vec->begin();
            iter != obj.vec->end(); ++iter) {
            if (!iter->type) continue;
            if (!first) oss<<',';
            pub_obj(*iter, oss);
            first = false;
        }
        oss<<']';
        break;
    case JSON_STRING:
        oss << '\"' <<(*obj.str)<<'\"';
        break;
    case JSON_PRIMITIVE:
        oss << (*obj.str);
        break;
    }
}
bool json_o::from(const std::string& json)
{
    return json_obj(*this, json.c_str(), json.size());
}
bool json_o::fromv(const char *format, va_list argv)
{
    return json_obj(*this, format, argv);
}
json_o& json_o::operator[](const std::string& key)
{
    if (!type) {
        type = JSON_OBJECT; 
        map = new json_map;
    }
    assert(type == JSON_OBJECT);
    return (*map)[key];
}
json_o& json_o::operator[](const int index)
{
    if (!type) {
        type = JSON_ARRAY; 
        vec = new json_vec;
    }
    assert(index >= 0 && type==JSON_ARRAY);
    if ((int)vec->size() <= index)
        vec->resize(index+1);
    return (*vec)[index];
}
std::string json_o::to_str() const
{
    std::ostringstream oss;
    assert(type);
    pub_obj(*this, oss);
    return oss.str();
}
void json_o::clean() {
    switch(type) {
    case JSON_OBJECT:
        delete map;
        map = NULL;
        break;
    case JSON_ARRAY:
        delete vec;
        vec = NULL;
        break;
    case JSON_STRING:
    case JSON_PRIMITIVE:
        delete str;
        str = NULL;
        break;
    }
    type = 0;
}
const json_o json_o::null;

enum State {ESCAPED, UNESCAPED};

std::string json_o::escape(const std::string& input)
{
    std::string output;
    output.reserve(input.length());

    for (std::string::size_type i = 0; i < input.length(); ++i)
    {
        switch (input[i]) {
        case '"':
            output += "\\\"";
            break;
        //case '/':
        //    output += "\\/";
        //    break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        case '\\':
            output += "\\\\";
            break;
        default:
            output += input[i];
            break;
        }

    }
    return output;
}
std::string json_o::unescape(const std::string& input)
{
    State s = UNESCAPED;
    std::string output;
    output.reserve(input.length());

    for (std::string::size_type i = 0; i < input.length(); ++i)
    {
        switch(s)
        {
        case ESCAPED:
            {
                switch(input[i])
                {
                case '"':
                    output += '\"';
                    break;
                //case '/':
                //    output += '/';
                //    break;
                case 'b':
                    output += '\b';
                    break;
                case 'f':
                    output += '\f';
                    break;
                case 'n':
                    output += '\n';
                    break;
                case 'r':
                    output += '\r';
                    break;
                case 't':
                    output += '\t';
                    break;
                case '\\':
                    output += '\\';
                    break;
                default:
                    output += input[i];
                    break;
                }

                s = UNESCAPED;
                break;
            }
        case UNESCAPED:
            {
                switch(input[i])
                {
                case '\\':
                    s = ESCAPED;
                    break;
                default:
                    output += input[i];
                    break;
                }
            }
        }
    }
    return output;
}
