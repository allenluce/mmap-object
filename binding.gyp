{
  "targets": [
    {
      "target_name": "mmap-object",
      "sources": [
        "mmap-object.cc",
        "cell.cc"
      ],
      "include_dirs": [
        "<@(include_dirs)",
        "<!(node -e \"require('nan')\")"
      ],
      "libraries": [
        "<@(libraries)"
      ],
      "cflags_cc": [
        "-Wall",
        "-Werror",
        "-O3",
        "-fexceptions",
        "-frtti",
        "-Wno-cast-function-type",
        "-Wno-deprecated-declarations"
      ],
      "conditions": [
        [
          "OS=='win'", {
            "variables": {
              "include_dirs": [
                "<!(echo %BOOST_ROOT%)"
              ],
              "libraries": []
            }
          }
        ],
        [
          "OS=='linux'", {
            "variables": {
              "include_dirs": [],
              "libraries": [
                "-lrt"
              ]
            }
          }
        ],
        [
          "OS=='mac' and '<!(uname -m)'=='arm64'", {
            "variables": {
              "include_dirs": [
                "/opt/homebrew/include"
              ],
              "libraries": [
                "-L/opt/homebrew/lib",
                "-lboost_system-mt",
                "-lboost_thread-mt"
              ]
            }
          }
        ],
        [
          "OS=='mac' and '<!(uname -m)'=='x86_64'", {
            "variables": {
              "include_dirs": [],
              "libraries": []
            }
          }
        ]
      ],
      "xcode_settings": {
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "GCC_ENABLE_CPP_RTTI": "-frtti"
      },
      "msvs_settings": {
        "VCLinkerTool": {
          "AdditionalLibraryDirectories": [
            "<!(echo %BOOST_ROOT%/stage/lib)"
          ]
        }
      }
    },
    {
      "target_name": "action_after_build",
      "type": "none",
      "dependencies": [ "<(module_name)" ],
      "copies": [
        {
          "files": [ "<(PRODUCT_DIR)/<(module_name).node" ],
          "destination": "<(module_path)"
        }
      ]
    }
  ]
}
