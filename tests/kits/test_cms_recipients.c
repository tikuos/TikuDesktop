/* Unsupported RecipientInfo tags cannot hide a later RSA recipient. */
#define main original_cms_main
#include "test_cms.c"
#undef main
#include <tiku_cms.c>
#include <assert.h>
int main(void)
{
    tiku_key_t *key = NULL;
    assert(key_of(ck_p12_modern, "p\xc3\xa4ssw\xc3\xb6rd", &key) == TIKU_KEY_OK);
    blob_t b = blob(ck_env_both);
    tiku_der_t recipients, content, first, second;
    tiku_der_cursor_t c;
    assert(envelope(b.p, b.n, &recipients, &content) == 0);
    tiku_der_enter(&recipients, &c);
    assert(tiku_der_next(&c, &first) == 0 && tiku_der_next(&c, &second) == 0);
    unsigned char *plain = NULL;
    if (recipient(key, &first, 1, &content, &plain) >= 0) {
        unsigned char *copy = malloc(recipients.len); assert(copy);
        memcpy(copy, second.raw, second.rawlen);
        memcpy(copy + second.rawlen, first.raw, first.rawlen);
        memcpy((unsigned char *)recipients.body, copy, recipients.len);
        free(copy); free(plain); plain = NULL;
    }
    *(unsigned char *)recipients.body = 0xa1;
    long n = tiku_cms_open(key, b.p, b.n, &plain);
    blob_t expected = blob(ck_message);
    int ok = n == (long)expected.n && plain && !memcmp(plain, expected.p, expected.n);
    free(expected.p); free(plain); free(b.p); tiku_key_free(key);
    puts(ok ? "PASS D-152" : "FAIL D-152"); return !ok;
}
