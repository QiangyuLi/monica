/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
Authors: 
Michael Berg <michael.berg@zalf.de>

Maintainers: 
Currently maintained by the authors.

This file is part of the util library used by models created at the Institute of 
Landscape Systems Analysis at the ZALF.
Copyright (C) 2007-2013, Leibniz Centre for Agricultural Landscape Research (ZALF)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef RUN_LANDCARE_DSS

#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <set>
#include <sstream>
#include <fstream>
#include <mutex>

#include "db/abstract-db-connections.h"
#include "monica-eom.h"
#include "tools/helper.h"

using namespace monica;
using namespace std;
using namespace Tools;
using namespace Eom;
using namespace Db;

namespace
{
  typedef map<PVPId, EomPVPInfo> PVPId2CropIdMap;
  const PVPId2CropIdMap& eomPVPId2cropIdMap()
  {
    static mutex lockable;
    typedef map<PVPId, EomPVPInfo> M;
    static M m;
    static bool initialized = false;
    if(!initialized)
    {
      lock_guard<mutex> lock(lockable);

      if(!initialized)
      {
        Db::DB *con = Db::newConnection("eom");

        con->select("select fa.pvpnr, m.id as crop_id, fa.faktor, "
                   "pvp.bbnr as tillage_type "
                   "from PVPfl_Fa as fa inner join PVPflanze as pvp on "
                   "fa.pvpnr = pvp.pvpnr inner join FA_Modelle as m on "
                   "fa.famnr = m.famnr "
                   "where btnr = 1 and m.modell = 1");

        DBRow row;
        while(!(row = con->getRow()).empty())
        {
          EomPVPInfo i;
          i.pvpId = satoi(row[0]);
					if(row[1].empty())
            continue;
          i.cropId = satoi(row[1]);
          i.crossCropAdaptionFactor = satof(row[2]);
          i.tillageType = TillageType(satoi(row[3]));
          //cout << "pvpId: " << i.pvpId << " cropId: " << i.cropId << " tt: " << i.tillageType << endl;

          m[i.pvpId] = i;
        }

        delete con;

        initialized = true;
      }
    }

    return m;
  }

}

EomPVPInfo monica::eomPVPId2cropId(PVPId pvpId) 
{
	PVPId2CropIdMap::const_iterator ci = eomPVPId2cropIdMap().find(pvpId);
	return ci != eomPVPId2cropIdMap().end() ? ci->second : EomPVPInfo();
}

string monica::eomOrganicFertilizerId2monicaOrganicFertilizerId(int eomId)
{
  static mutex lockable;

  static bool initialized = false;
  typedef map<int, string> M;
  M m;

  if(!initialized)
  {
    lock_guard<mutex> lock(lockable);

    if (!initialized)
    {
			DBPtr con(newConnection("landcare-dss"));
      DBRow row;

      con->select("select "
                  "eom_id, "
                  "monica_id "
                  "from eom_2_monica_organic_fertilizer_id");

      while(!(row = con->getRow()).empty())
        m[satoi(row[0])] = row[1];
    }
  }

  auto ci = m.find(eomId);
  return ci != m.end() ? ci->second : "";
}

#endif
