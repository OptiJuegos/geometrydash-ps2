/* Exercising the actual json-c compatibility layer with the vendored cJSON.
 * This runs on the host; PS2 compilation of both units is checked separately.
 */
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>

static int require(int condition, const char *message)
{
    if (condition) return 1;
    fprintf(stderr, "JSON smoke failed: %s\n", message);
    return 0;
}

int main(void)
{
    struct json_object *root = json_object_new_object();
    struct json_object *options = json_object_new_object();
    struct json_object *parsed = NULL;
    struct json_object *node = NULL;
    const char *json;
    if (!require(root && options, "allocate objects")) return 1;
    if (!require(json_object_object_add(root, "settings", options) == 0,
                 "attach nested object")) return 2;
    json_object_object_add(options, "volume", json_object_new_double(0.75));
    json_object_object_add(options, "enabled", json_object_new_boolean(1));
    json_object_object_add(options, "username", json_object_new_string("Player 1"));
    json_object_object_add(options, "lives", json_object_new_int(42));
    json_object_object_add(options, "lives", json_object_new_int(9));

    json = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    if (!require(json && strstr(json, "Player 1"), "serialize")) return 3;
    parsed = json_tokener_parse(json);
    if (!require(parsed != NULL, "parse output")) return 4;
    if (!require(json_object_object_get_ex(parsed, "settings", &options),
                 "nested settings")) return 5;
    if (!require(json_object_object_get_ex(options, "lives", &node) &&
                 json_object_get_int(node) == 9, "replacement semantics")) return 6;
    if (!require(json_object_object_get_ex(options, "enabled", &node) &&
                 json_object_get_boolean(node), "boolean")) return 7;
    if (!require(json_object_object_get_ex(options, "volume", &node) &&
                 json_object_get_double(node) > 0.74 &&
                 json_object_get_double(node) < 0.76, "floating point")) return 8;
    if (!require(json_object_object_get_ex(options, "username", &node) &&
                 strcmp(json_object_get_string(node), "Player 1") == 0,
                 "string")) return 9;
    if (!require(!json_object_object_get_ex(options, "missing", &node) &&
                 node == NULL, "missing key")) return 10;
    if (!require(json_tokener_parse("{broken json") == NULL,
                 "reject malformed JSON")) return 11;

    json_object_put(parsed);
    json_object_put(root);
    puts("PASS: JSON create, replace, print, parse, typed values and invalid input");
    return 0;
}
