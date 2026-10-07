/* Component function arrays produce one output per member. */
#include "tiku_pdf_private.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int refuses(const char *text)
{
    pdf_cur_t cursor = {(const unsigned char *)text, (long)strlen(text), 0};
    pdf_obj_t *spec = pdf_parse(&cursor, 0);
    pdf_func_t *f = pdf_func_load(NULL, spec);
    int refused = f == NULL;
    pdf_func_free(f); pdf_free(spec);
    return refused;
}
int main(void)
{
    const char *text = "[ << /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >>"
        " << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>"
        " << /FunctionType 2 /Domain [0 1] /C0 [.25] /C1 [.75] /N 1 >> ]";
    pdf_cur_t cursor = {(const unsigned char *)text, (long)strlen(text), 0};
    pdf_obj_t *spec = pdf_parse(&cursor, 0);
    pdf_func_t *f = pdf_func_load(NULL, spec);
    double x = .25, out[8] = {0};
    int ok = f && pdf_func_eval(f, &x, 1, out) == 3 &&
             fabs(out[0] - .25) < 1e-9 && fabs(out[1] - .75) < 1e-9 &&
             fabs(out[2] - .375) < 1e-9;
    pdf_func_free(f); pdf_free(spec);
    ok &= refuses("[]") && refuses("[42]") && refuses(
        "[<< /FunctionType 2 /Domain [0 1] /C0 [0 1] /C1 [1 0] /N 1 >>]");
    char nested[512] = {0};
    for (int i = 0; i < 20; i++) strcat(nested, "[");
    strcat(nested, "<< /FunctionType 2 /Domain [0 1] /N 1 >>");
    for (int i = 0; i < 20; i++) strcat(nested, "]");
    ok &= refuses(nested);
    puts(ok ? "PASS D-091" : "FAIL D-091"); return !ok;
}
