#include <iostream>

#include "lfs_interface.h"

#pragma comment(lib, "littlefs_v2.lib")

int main() {

    { // create vfs

        std::shared_ptr< fs::IFileSystemDevice> filesystem;

        if (fs::createVFS(L"test.fs", filesystem, fs::lfsVFS::Backend::kFileBackend) == fs::kCodeOK) {

            // create file and write some thing
            std::shared_ptr<fs::IFileObject> file_handle;

            if (filesystem->openFile(file_handle, "test_file",
                fs::kFileRead |
                fs::kFileWrite |
                fs::kFileCreateIfNotExists) == fs::kCodeOK) {


                file_handle->write("hello test", sizeof("hello test"));
            }
        }
    }

    { //open vfs

        std::shared_ptr< fs::IFileSystemDevice> filesystem;

        if (fs::openVFS(L"test.fs", filesystem, fs::lfsVFS::Backend::kFileBackend) == fs::kCodeOK) {

            { // get files list

                auto entries = filesystem->dir("/");

                for (auto& entry : entries) {

                    std::cout << entry.getPath() << "  " << entry.getSize() << std::endl;
                }
            }

            // open file and read
            std::shared_ptr<fs::IFileObject> file_handle;

            if (filesystem->openFile(file_handle, "test_file",
                fs::kFileRead |
                fs::kFileWrite |
                fs::kFileCreateIfNotExists) == fs::kCodeOK) {

                char test[sizeof("hello test")];

                file_handle->read(test, sizeof("hello test"));

                std::cout << test << std::endl;
            }

            if (filesystem->deleteFile("test_file") == fs::kCodeOK) {
                // ...
            }


            { // get files list

                auto entries = filesystem->dir("/");

                for (auto& entry : entries) {

                    std::cout << entry.getPath() << "  " << entry.getSize() << std::endl;
                }
            }
        }
    }

    return 0;
}
