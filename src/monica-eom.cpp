#ifdef TEST_LANDCARE_DSS

#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <set>
#include <sstream>
#include <fstream>

#define LOKI_OBJECT_LEVEL_THREADING

#include "loki/Threads.h"

#include <boost/foreach.hpp>
#include "tools/use-stl-algo-boost-lambda.h"
#include "db/abstract-db-connections.h"
#include "monica-eom.h"
#include "tools/helper.h"

using namespace Monica;
using namespace std;
using namespace Tools;
using namespace Eom;
using namespace Db;

namespace
{
  struct L : public Loki::ObjectLevelLockable<L> {};

  typedef map<PVPId, EomPVPInfo> PVPId2CropIdMap;
  const PVPId2CropIdMap& eomPVPId2cropIdMap()
  {
    static L lockable;
    typedef map<PVPId, EomPVPInfo> M;
    static M m;
    static bool initialized = false;
    if(!initialized)
    {
      L::Lock lock(lockable);

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

EomPVPInfo Monica::eomPVPId2cropId(PVPId pvpId) 
{
	PVPId2CropIdMap::const_iterator ci = eomPVPId2cropIdMap().find(pvpId);
	return ci != eomPVPId2cropIdMap().end() ? ci->second : EomPVPInfo();
}

int Monica::eomOrganicFertilizerId2monicaOrganicFertilizerId(int eomId)
{
  static L lockable;

  static bool initialized = false;
  typedef map<int, int> M;
  M m;

  if(!initialized)
  {
    L::Lock lock(lockable);

    if (!initialized)
    {
			DBPtr con(newConnection("landcare-dss"));
      DBRow row;

      con->select("select eom_id, monica_id "
                  "from eom_2_monica_organic_fertilizer_id");

      while(!(row = con->getRow()).empty())
        m[satoi(row[0])] = satoi(row[1]);
    }
  }

  M::const_iterator ci = m.find(eomId);
  return ci != m.end() ? ci->second : -1;
}

#endif /*#ifdef TEST_LANDCARE_DSS*/
