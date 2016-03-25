/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "Shorten.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "Debug.h"
#include "DexClass.h"
#include "DexLoader.h"
#include "DexOutput.h"
#include "DexUtil.h"
#include "Transform.h"
#include "walkers.h"
#include "Warning.h"

static bool maybe_file_name(const char* str, size_t len) {
  if (len < 5) return false;
  return strncmp(str + len - 5, ".java", 5) == 0;
}

static bool is_reasonable_string(const char* str, size_t len) {
  std::vector<char> avoid = {'\n', '\t', ':', ','};
  if (len == 0) return false;
  for (size_t i = 0; i < len; i++) {
    for (auto c : avoid) {
      if (str[i] == c) {
        return false;
      }
    }
  }
  return true;
}

DexString* get_suitable_string(std::unordered_set<DexString*>& set,
                               std::vector<DexString*>& dex_strings) {
  while (dex_strings.size()) {
    DexString* val = dex_strings.back();
    dex_strings.pop_back();
    auto valstr = val->c_str();
    auto vallen = strlen(valstr);
    auto not_file_name = !maybe_file_name(valstr, vallen);
    auto no_bad_char = is_reasonable_string(valstr, vallen);
    auto not_seen_yet = !set.count(val);
    if (not_seen_yet && not_file_name && no_bad_char) {
      return val;
    }
  }
  return nullptr;
}

static void strip_src_strings(DexClassesVector& dexen, const char* map_path) {
  size_t shortened = 0;
  size_t string_savings = 0;
  std::unordered_map<DexString*, std::vector<DexString*>> global_src_strings;
  std::unordered_set<DexString*> shortened_used;

  for (auto& classes : dexen) {
    std::unordered_map<DexString*, DexString*> src_to_shortened;
    std::vector<DexString*> current_dex_strings;
    for (auto const& clazz : classes) {
      clazz->gather_strings(current_dex_strings);
    }
    sort_unique(current_dex_strings, compare_dexstrings);

    for (auto const& clazz : classes) {
      auto src_string = clazz->get_source_file();
      if (!src_string) {
        continue;
      }
      DexString* shortened_src_string = nullptr;
      if (src_to_shortened.count(src_string) == 0) {
        shortened_src_string =
            get_suitable_string(shortened_used, current_dex_strings);
        if (!shortened_src_string) {
          opt_warn(UNSHORTENED_SRC_STRING, "%s\n", SHOW(src_string));
          shortened_src_string = src_string;
        } else {
          shortened++;
          string_savings += strlen(src_string->c_str());
        }
        src_to_shortened[src_string] = shortened_src_string;
        shortened_used.emplace(shortened_src_string);
        global_src_strings[src_string].push_back(shortened_src_string);
      } else {
        shortened_src_string = src_to_shortened[src_string];
      }
      clazz->set_source_file(shortened_src_string);
    }
  }

  TRACE(SHORTEN, 1, "src strings shortened %ld, %lu bytes saved\n", shortened,
      string_savings);

  // generate mapping
  FILE* fd = fopen(map_path, "w");
  if (fd == nullptr) {
    perror("Error writing mapping file");
    return;
  }

  for (auto it : global_src_strings) {
    auto desc_vector = it.second;
    std::sort(desc_vector.begin(), desc_vector.end());
    std::unique(desc_vector.begin(), desc_vector.end());
    fprintf(fd, "%s ->", it.first->c_str());
    for (auto str : desc_vector) {
      fprintf(fd, " %s,", str->c_str());
    }
    fprintf(fd, "\n");
  }
  fclose(fd);
}

void ShortenSrcStringsPass::run_pass(DexClassesVector& dexen, PgoFiles& pgo) {
  const char* path = nullptr;
  if (m_config.isObject()) {
    auto fmapit = m_config.find("filename_mappings");
    if (fmapit != m_config.items().end()) {
      path = fmapit->second.c_str();
    }
  }
  if (!path) {
    path = "/tmp/filename_mappings.txt";
  }
  return strip_src_strings(dexen, path);
}