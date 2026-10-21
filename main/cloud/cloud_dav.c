/* SPDX-License-Identifier: Apache-2.0
 * 中文：Expat按4KiB分块解析，禁DTD/实体，限制深度、字段、响应数与目录大小。
 * 列出可读内容、PNG/JPEG图片及TTF字体，旧清单和版本目录不显示。
 * List reading files, PNG/JPEG images and TTF fonts; hide legacy manifests/version directories.
 * English: Expat parses 4KiB chunks with DTD/entities disabled and bounded depth, fields, responses and
 * listing size.
 */
#include "cloud_dav.h"
#include "sync_file_policy.h"
#include <ctype.h>
#include <expat.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef struct {
    XML_Parser parser;
    const char *folder;
    cloud_dav_entry_t *out;
    size_t capacity, count, responses;
    unsigned depth, response_depth, propstat_depth, capture_depth;
    bool failed, truncated, root, collection, success;
    char field[32], text[769], href[769];
    size_t text_length;
    cloud_dav_entry_t item, properties;
} dav_parser_t;
static const char *dav_name(const char *name) {
    return !strncmp(name, "DAV:|", 5) ? name + 5 : "";
}
static void stop(dav_parser_t *p) {
    p->failed = true;
    XML_StopParser(p->parser, XML_FALSE);
}
static int hex(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
static bool child_href(const char *source, const char *folder, char out[241]) {
    const char *origin = "https://dav.jianguoyun.com";
    size_t origin_length = strlen(origin);
    if (!strncmp(source, origin, origin_length) && source[origin_length] == '/')
        source += origin_length;
    if (*source != '/')
        return false;
    char decoded[769];
    size_t n = 0;
    for (const char *p = source; *p; ++p) {
        unsigned c = (unsigned char)*p;
        if (*p == '%') {
            if (!p[1] || !p[2] || hex(p[1]) < 0 || hex(p[2]) < 0)
                return false;
            c = hex(p[1]) * 16 + hex(p[2]);
            p += 2;
        }
        if (!c || c < 32 || c == 127 || n + 1 >= sizeof(decoded))
            return false;
        decoded[n++] = c;
    }
    decoded[n] = 0;
    size_t base = strlen(folder);
    if (strncmp(decoded, folder, base) || !decoded[base] || strchr(decoded + base, '/') ||
        !cloud_relative_path(decoded + base) || strlen(decoded + base) > 240)
        return false;
    strcpy(out, decoded + base);
    return true;
}
static void XMLCALL start(void *data, const char *name, const char **attrs) {
    (void)attrs;
    dav_parser_t *p = data;
    const char *tag = dav_name(name);
    if (++p->depth > 16) {
        stop(p);
        return;
    }
    if (p->depth == 1) {
        p->root = !strcmp(tag, "multistatus");
        if (!p->root)
            stop(p);
    }
    if (!strcmp(tag, "response")) {
        if (p->response_depth || ++p->responses > 750) {
            stop(p);
            return;
        }
        p->response_depth = p->depth;
        memset(&p->item, 0, sizeof(p->item));
        p->href[0] = 0;
        p->collection = false;
    }
    if (!p->response_depth)
        return;
    if (!strcmp(tag, "propstat")) {
        p->propstat_depth = p->depth;
        memset(&p->properties, 0, sizeof(p->properties));
        p->success = false;
    }
    if (!strcmp(tag, "collection"))
        p->collection = true;
    if (!strcmp(tag, "href") || !strcmp(tag, "getetag") || !strcmp(tag, "creationdate") ||
        !strcmp(tag, "getlastmodified") || !strcmp(tag, "getcontentlength") || !strcmp(tag, "status")) {
        if (p->capture_depth) {
            stop(p);
            return;
        }
        snprintf(p->field, sizeof(p->field), "%s", tag);
        p->capture_depth = p->depth;
        p->text_length = 0;
        p->text[0] = 0;
    }
}
static void XMLCALL text(void *data, const char *value, int length) {
    dav_parser_t *p = data;
    if (!p->capture_depth)
        return;
    if (length < 0 || p->text_length + (size_t)length >= sizeof(p->text)) {
        stop(p);
        return;
    }
    memcpy(p->text + p->text_length, value, length);
    p->text_length += length;
    p->text[p->text_length] = 0;
}
static void XMLCALL end(void *data, const char *name) {
    dav_parser_t *p = data;
    const char *tag = dav_name(name);
    if (p->capture_depth == p->depth) {
        const char *v = p->text;
        while (isspace((unsigned char)*v))
            ++v;
        size_t n = strlen(v);
        while (n && isspace((unsigned char)v[n - 1]))
            --n;
        char value[769];
        memcpy(value, v, n);
        value[n] = 0;
        cloud_dav_entry_t *item = p->propstat_depth ? &p->properties : &p->item;
        if (!strcmp(tag, "href"))
            snprintf(p->href, sizeof(p->href), "%s", value);
        else if (!strcmp(tag, "status")) {
            p->success = strstr(value, " 200 ") != NULL;
        } else if (!strcmp(tag, "getetag")) {
            if (n >= sizeof(item->etag))
                stop(p);
            else
                strcpy(item->etag, value);
        } else if (!strcmp(tag, "getlastmodified")) {
            if (n >= sizeof(item->modified))
                stop(p);
            else
                strcpy(item->modified, value);
        } else if (!strcmp(tag, "creationdate")) {
            if (n < sizeof(item->created_at) && cloud_timestamp(value, &item->created_ms))
                strcpy(item->created_at, value);
        } else if (!strcmp(tag, "getcontentlength")) {
            char *last;
            unsigned long long bytes = strtoull(value, &last, 10);
            if (!*value || *last || *value == '-')
                stop(p);
            else
                item->bytes = bytes;
        }
        p->capture_depth = 0;
    }
    if (!strcmp(tag, "propstat") && p->propstat_depth == p->depth) {
        if (p->success) {
            if (p->properties.etag[0])
                strcpy(p->item.etag, p->properties.etag);
            if (p->properties.modified[0])
                strcpy(p->item.modified, p->properties.modified);
            if (p->properties.created_at[0]) {
                strcpy(p->item.created_at, p->properties.created_at);
                p->item.created_ms = p->properties.created_ms;
            }
            p->item.bytes = p->properties.bytes;
        }
        p->propstat_depth = 0;
    }
    if (!strcmp(tag, "response") && p->response_depth == p->depth) {
        if (!p->collection && child_href(p->href, p->folder, p->item.href)) {
            bool readable =
                sync_file_kind(p->item.href) != SYNC_FILE_UNSUPPORTED &&
                document_store_safe_filename(p->item.href);
            if (readable) {
                p->item.date_fallback = !p->item.created_ms;
                if (p->count == p->capacity)
                    p->truncated = true;
                else
                    p->out[p->count++] = p->item;
            }
        }
        p->response_depth = 0;
    }
    if (p->depth)
        --p->depth;
}
static void XMLCALL doctype(void *data, const char *name, const char *system, const char *public_id,
                            int subset) {
    (void)name;
    (void)system;
    (void)public_id;
    (void)subset;
    stop(data);
}
esp_err_t cloud_dav_parse(FILE *file, const char *folder, cloud_dav_entry_t *out, size_t capacity,
                          size_t *count, bool *truncated) {
    if (!file || !folder || *folder != '/' || folder[strlen(folder) - 1] != '/' || !out || !capacity ||
        capacity > CLOUD_ENTRY_MAX || !count || !truncated)
        return ESP_ERR_INVALID_ARG;
    *count = 0;
    *truncated = false;
    dav_parser_t *p = calloc(1, sizeof(*p));
    if (!p)
        return ESP_ERR_NO_MEM;
    p->folder = folder;
    p->out = out;
    p->capacity = capacity;
    p->parser = XML_ParserCreateNS("UTF-8", '|');
    if (!p->parser) {
        free(p);
        return ESP_ERR_NO_MEM;
    }
    XML_SetUserData(p->parser, p);
    XML_SetElementHandler(p->parser, start, end);
    XML_SetCharacterDataHandler(p->parser, text);
    XML_SetStartDoctypeDeclHandler(p->parser, doctype);
    XML_SetParamEntityParsing(p->parser, XML_PARAM_ENTITY_PARSING_NEVER);
    char buffer[4096];
    size_t total = 0, n;
    bool ok = true;
    while ((n = fread(buffer, 1, sizeof(buffer), file))) {
        total += n;
        if (total > CLOUD_XML_MAX || XML_Parse(p->parser, buffer, n, 0) == XML_STATUS_ERROR) {
            ok = false;
            break;
        }
    }
    if (ferror(file) || !ok || p->failed || XML_Parse(p->parser, "", 0, 1) == XML_STATUS_ERROR || !p->root)
        ok = false;
    *count = p->count;
    *truncated = p->truncated || p->responses == 750;
    XML_ParserFree(p->parser);
    free(p);
    return ok ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
