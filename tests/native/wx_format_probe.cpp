#include <wx/string.h>
#include <wx/debug.h>
#include <cstdarg>
#include <climits>
#include <cstdio>
static int assertions = 0;
static void OnAssert(const wxString&, int, const wxString&, const wxString&, const wxString&) { ++assertions; }
static wxString legacy(bool convert, const wchar_t* format, ...) {
    va_list args; va_start(args, format);
    int value = va_arg(args, int);
    wchar_t text[512] = {};
    if (convert) wxSnprintf(text, 512, format, static_cast<long>(value));
    else wxSnprintf(text, 512, format, value);
    va_end(args);
    return wxString(text);
}
int main() {
    wxSetAssertHandler(OnAssert);
    legacy(false, L"Layer %ld", 1);
    if (!assertions) return 1;
    assertions = 0;
    if (legacy(true, L"Layer %ld", 1) != L"Layer 1") return 2;
    if (legacy(true, L"%ld", INT_MIN) != L"-2147483648") return 3;
    wchar_t text[512] = {};
    wxSnprintf(text, 512, L"%lu", static_cast<unsigned long>(UINT_MAX));
    if (wxString(text) != L"4294967295" || assertions) return 4;
    puts("Original mismatch reproduced; corrected signed/unsigned formats pass without assertions.");
}
