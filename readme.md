littlefs v2
====

Refactored [littlefs](https://github.com/littlefs-project/littlefs) with extensions for use as virtual filesystem by my needs.

- Refactored code
- Added support for file size up to 0x7FFFFFFFFFFFFFFF
- Added disk auto-grow, without remount
- Has 2 backend, for use in memory and use in file as virtual file system

I will not support it later, because the performance in the test turned out to be too bad with random access to the file (╯ ° □ °) ╯ (┻━┻)