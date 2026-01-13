############################################################################
# multimedia/media/scripts/gdb/mediapolicy.py
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

import gdb
from nxgdb import utils

# Constants from internal.h
PFW_CRITERION_EXCLUSIVE = 1
PFW_CRITERION_INCLUSIVE = 2
PFW_CRITERION_NUMERICAL = 3

class MediaPolicyHandler:
    def __init__(self, handle):
        self.system = None

        # Cache types
        try:
            self.char_ptr_type = gdb.lookup_type("char").pointer()
            self.char_ptr_ptr_type = self.char_ptr_type.pointer()

            self.criterion_type = gdb.lookup_type("struct pfw_criterion_s")
            self.criterion_ptr_type = self.criterion_type.pointer()
            self.criterion_ptr_ptr_type = self.criterion_ptr_type.pointer()

            self.domain_type = gdb.lookup_type("struct pfw_domain_s")
            self.domain_ptr_type = self.domain_type.pointer()
            self.domain_ptr_ptr_type = self.domain_ptr_type.pointer()
        except gdb.error:
            # Fallback types if structs are not found
            self.char_ptr_type = None
            self.char_ptr_ptr_type = None
            self.criterion_ptr_ptr_type = None
            self.domain_ptr_ptr_type = None

        # handle is void*
        # Try to cast to MediaPolicyPriv* first to get the policy pointer
        try:
            # Try both typedef and struct name
            try:
                priv_type = gdb.lookup_type("MediaPolicyPriv").pointer()
            except gdb.error:
                priv_type = gdb.lookup_type("struct MediaPolicyPriv").pointer()

            priv = handle.cast(priv_type)
            self.system = priv['policy']
        except gdb.error:
            pass

        if not self.system:
             # Fallback: try to treat handle as pfw_system_t* directly
            try:
                self.system = handle.cast(gdb.lookup_type("struct pfw_system_s").pointer())
            except gdb.error:
                try:
                    self.system = handle.cast(gdb.lookup_type("pfw_system_t").pointer())
                except gdb.error:
                    gdb.write("Error: Could not resolve type for MediaPolicyPriv or pfw_system_t.\n")
                    self.system = None

    def get_criterion_value(self, criterion):
        ctype = int(criterion['type'])
        state = int(criterion['state'])

        # ranges is pfw_vector_t*
        ranges_vec = criterion['ranges']

        if ctype == PFW_CRITERION_NUMERICAL:
            return ""

        if not self.char_ptr_ptr_type:
            return ""

        ranges_arr = ranges_vec['eles'].cast(self.char_ptr_ptr_type)
        max_bits = min(int(ranges_vec['cnt']), 31)
        if max_bits <= 0:
            return ""

        if ctype == PFW_CRITERION_EXCLUSIVE:
            if 0 <= state < max_bits:
                val_ptr = ranges_arr[state]
                return val_ptr.string() if val_ptr else ""
            return ""

        # Inclusive
        if state == 0:
            return "<none>"

        vals = [ranges_arr[i].string() if ranges_arr[i] else "" for i in range(max_bits)]
        res = []
        mask = state
        idx = 0
        while mask and idx < max_bits:
            if mask & 1 and vals[idx]:
                res.append(vals[idx])
            mask >>= 1
            idx += 1
        return "|".join(res)

    def dump_policy(self):
        if not self.system:
            return None

        lines = []

        # Criteria Table
        lines.append("+-------------------------------------------------------------")
        lines.append(f"| {'CRITERIA':<32} | {'STATE':<8} | VALUE")
        lines.append("+-------------------------------------------------------------")

        criteria_vec = self.system['criteria']
        cnt = int(criteria_vec['cnt'])

        if cnt > 0 and self.criterion_ptr_ptr_type:
            criteria_arr = criteria_vec['eles'].cast(self.criterion_ptr_ptr_type)

            for criterion in utils.ArrayIterator(criteria_arr, cnt):
                if not criterion:
                    continue

                names_vec = criterion['names']
                names_arr = names_vec['eles'].cast(self.char_ptr_ptr_type)
                name_ptr = names_arr[0]
                name = name_ptr.string() if name_ptr else "?"

                state = int(criterion['state'])
                value = self.get_criterion_value(criterion)

                lines.append(f"| {name:<32} | {state:<8} | {value}")

        lines.append("+-------------------------------------------------------------")

        # Domain Table
        lines.append(f"| {'DOMAIN':<32} | CONFIG")
        lines.append("+-------------------------------------------------------------")

        domains_vec = self.system['domains']
        cnt = int(domains_vec['cnt'])

        if cnt > 0 and self.domain_ptr_ptr_type:
            domains_arr = domains_vec['eles'].cast(self.domain_ptr_ptr_type)

            for domain in utils.ArrayIterator(domains_arr, cnt):
                if not domain:
                    continue

                name = domain['name'].string()

                current_config = domain['current']
                config_name = ""
                if current_config:
                    config_name = current_config['current'].string()

                lines.append(f"| {name:<32} | {config_name}")

        lines.append("+-------------------------------------------------------------")

        return lines
