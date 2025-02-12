![](https://github.com/sumatrapdfreader/sumatrapdf/workflows/Build/badge.svg)

## SumatraPDF Reader

SumatraPDF is a multi-format (PDF, EPUB, MOBI, FB2, CHM, XPS, DjVu) reader
for Windows under (A)GPLv3 license, with some code under BSD license (see
AUTHORS).

More information:
* [main website](https://www.sumatrapdfreader.org/free-pdf-reader) with downloads and documentation
* [manual](https://www.sumatrapdfreader.org/manual)

To compile you need latest Visual Studio 2022. [Free Community edition](https://www.visualstudio.com/vs/community/) works.

Open `vs2022/SumatraPDF.sln` and hit F5 to compile and run.

For best results use the latest release available as that's what I use and test with.
If things don't compile, first make sure you're using the latest version of Visual Studio.

Here's more [developer information](https://www.sumatrapdfreader.org/docs/Contribute-to-SumatraPDF).

Notes on targets:
* `asan` target is for enabling address sanitizer

### Asan notes

Docs:
* https://docs.microsoft.com/en-us/cpp/sanitizers/asan?view=msvc-160
* https://devblogs.microsoft.com/cppblog/asan-for-windows-x64-and-debug-build-support/
* https://devblogs.microsoft.com/cppblog/addresssanitizer-asan-for-windows-with-msvc/


Flags:
* https://github.com/google/sanitizers/wiki/SanitizerCommonFlags
* https://github.com/google/sanitizers/wiki/AddressSanitizerFlags

Can be set with env variable:
* `ASAN_OPTIONS=halt_on_error=0:allocator_may_return_null=1:verbosity=2:check_malloc_usable_size=false:print_suppressions=true:suppressions="C:\Users\kjk\src\sumatrapdf\asan.supp"`

In Visual Studio, this is in `Debugging`, `Environment` section.

Note:
* as of VS 16.6.2 `ASAN_OPTIONS=detect_leaks=1` (i.e. memory leaks) doesn't work.
  Unix version relies on tcmalloc so this might never work
Suppressing issues: https://clang.llvm.org/docs/AddressSanitizer.html#issue-suppression
Note: I couldn't get suppressing to work.
