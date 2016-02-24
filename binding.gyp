{
  "targets": [
    {
      "target_name": "<(module_name)",
      "type": "loadable_module",
      "cflags_cc": [
        "-Wall",
        "-Werror",
        "-O3",
        "-fexceptions",
        "-frtti"
      ],
      "sources": ["main.cc"],
      "include_dirs": [
        "<!(node -e \"require('nan')\")"
      ],
      "dependencies": [ "mmo-lib"]
    },
    {
      "target_name": "mmo-lib",
      "cflags_cc": [
        "-Wall",
        "-Werror",
        "-O3",
        "-fexceptions",
        "-frtti"
      ],
      "type": "static_library",
      "sources": [ "mmap-object.cc"],
      "include_dirs": [
        "<!(node -e \"require('nan')\")"
      ]
    },
    {
      "target_name": "action_after_build",
      "type": "none",
      "dependencies": [
        "<(module_name)"
      ],
      "copies": [
        {
          "files": [
            "<(PRODUCT_DIR)/<(module_name).node"
          ],
          "destination": "<(module_path)"
        }
      ]
    }
  ]
}
