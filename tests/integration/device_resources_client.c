/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * device_resources_client.c - public SDK board requests and queued cancellation.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main shell_client_main
#include "shell_resources_client.c"
#undef main
/** @brief Queue a complete context-guarded configuration proposal. */
static int queue(const char *path, const char *text, const char *token)
{
    tiku_msg_t *stamp = call(request("describe", path)),
               *message = request("set", path), *value = tiku_msg_new(0),
               *result = NULL;
    int ok = stamp && message && value &&
             tiku_msg_add_string(value, "type", "object") &&
             member(value, "value",
                    tiku_resource_value_string(TIKU_RESOURCE_TEXT, text)) &&
             member(value, "context",
                    tiku_resource_value_string(TIKU_RESOURCE_TEXT, token)) &&
             expected(message, stamp) &&
             tiku_msg_add_message(message, "value", value) &&
             tiku_msg_add_string(message, "durability",
                                 getenv("STOMME_TEST_DURABILITY")
                                     ? getenv("STOMME_TEST_DURABILITY")
                                     : "session") &&
             tiku_msg_add_int32(message, "deadline_ms", 3000);
    if (ok) {
        result = call(message);
        message = NULL;
    }
    const char *job = tiku_msg_find_string(result, "job", 0);
    if (job)
        puts(job);
    tiku_msg_free(stamp);
    tiku_msg_free(message);
    tiku_msg_free(value);
    tiku_msg_free(result);
    return job ? 0 : 1;
}
/** @brief Read one retained job and optionally request cancellation at its stamp. */
static int job_status(const char *id, int cancel_job)
{
    tiku_msg_t *message = request("job-status", NULL), *result = NULL;
    if (message && tiku_msg_add_string(message, "job", id))
        result = call(message);
    else
        tiku_msg_free(message);
    if (!result)
        return 1;
    if (cancel_job) {
        message = request("cancel", NULL);
        if (!message || !tiku_msg_add_string(message, "job", id) ||
            !expected(message, result) ||
            !tiku_msg_add_int32(message, "deadline_ms", 3000)) {
            tiku_msg_free(message);
            tiku_msg_free(result);
            return 1;
        }
        tiku_msg_free(result);
        result = call(message);
        if (!result)
            return 1;
    } else
        printf("%s\t%s\t%s\n", tiku_msg_find_string(result, "state", 0),
               tiku_msg_find_string(result, "code", 0),
               tiku_msg_find_string(result, "effect", 0));
    tiku_msg_free(result);
    return 0;
}
int main(int argc, char **argv)
{
    if (argc < 4)
        return 2;
    if (strcmp(argv[2], "queue") && strcmp(argv[2], "job-status") &&
        strcmp(argv[2], "cancel"))
        return shell_client_main(argc, argv);
    channel = tiku_resource_endpoint_connect(argv[1], 5000);
    if (!channel ||
        tiku_resource_channel_negotiate(channel,
                                        TIKU_RESOURCE_FEATURE_READ |
                                            TIKU_RESOURCE_FEATURE_CHANGE |
                                            TIKU_RESOURCE_FEATURE_JOBS,
                                        0, 5000)) {
        tiku_resource_channel_free(channel);
        return 1;
    }
    int status = 1;
    if (argc == 6 && !strcmp(argv[2], "queue"))
        status = queue(argv[3], argv[4], argv[5]);
    else if (argc == 4)
        status = job_status(argv[3], !strcmp(argv[2], "cancel"));
    tiku_resource_channel_free(channel);
    return status;
}
