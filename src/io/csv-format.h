/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
Authors: 
Claas Nendel <claas.nendel@zalf.de>
Xenia Specka <xenia.specka@zalf.de>
Michael Berg <michael.berg@zalf.de>

Maintainers: 
Currently maintained by the authors.

This file is part of the MONICA model. 
Copyright (C) Leibniz Centre for Agricultural Landscape Research (ZALF)
*/

#pragma once

#include <string>
#include <iostream>

#include "json11/json11.hpp"
#include "json11/json11-helper.h"
#include "../io/output.h"

namespace monica {
  void writeOutputHeaderRows(std::ostream& out,
                             const std::vector<OId>& outputIds,
                             std::string csvSep,
                             bool includeHeaderRow,
                             bool includeUnitsRow,
                             bool includeTimeAgg = true);

  void writeOutput(std::ostream& out,
                   const std::vector<OId>& outputIds,
                   const std::vector<Tools::J11Array>& values,
                   std::string csvSep);
  void writeOutputObj(std::ostream& out,
                      const std::vector<OId>& outputIds,
                      const std::vector<Tools::J11Object>& values,
                      std::string csvSep);
} // namespace monica

