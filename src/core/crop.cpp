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

#include <map>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cmath>
#include <utility>
#include <mutex>

#include "tools/helper.h"
#include "json11/json11-helper.h"
#include "tools/algorithms.h"
#include "../core/monica-parameters.h"
#include "tools/debug.h"
#include "../io/database-io.h"

#include "crop.h"

using namespace std;
using namespace Monica;
using namespace Tools;

//----------------------------------------------------------------------------

Crop::Crop(const std::string& speciesName)
  : _speciesName(speciesName)
{}

Crop::Crop(const std::string& species,
           const string& cultivarName,
           const CropParametersPtr cps,
           const CropResidueParametersPtr rps,
           double crossCropAdaptionFactor)
  : _speciesName(species)
  , _cultivarName(cultivarName)
  , _cropParams(cps)
  , _residueParams(rps)
  , _crossCropAdaptionFactor(crossCropAdaptionFactor)
{}

Crop::Crop(const string& speciesName,
           const string& cultivarName,
           const Tools::Date& seedDate,
           const Tools::Date& harvestDate,
           const CropParametersPtr cps,
           const CropResidueParametersPtr rps,
           double crossCropAdaptionFactor)
  : _speciesName(speciesName)
  , _cultivarName(cultivarName)
  , _seedDate(seedDate)
  , _harvestDate(harvestDate)
  , _cropParams(cps)
  , _residueParams(rps)
  , _crossCropAdaptionFactor(crossCropAdaptionFactor)
{}

Crop::Crop(json11::Json j)
{
	merge(j);
}

void Crop::deserialize(mas::models::monica::CropState::Reader reader) {

}

void Crop::serialize(mas::models::monica::CropState::Builder builder) const {

}

Errors Crop::merge(json11::Json j)
{
	Errors res = Json11Serializable::merge(j);

	set_iso_date_value(_seedDate, j, "seedDate");
	set_iso_date_value(_harvestDate, j, "havestDate");
	set_int_value(_dbId, j, "id");
	set_string_value(_speciesName, j, "species");
	set_string_value(_cultivarName, j, "cultivar");

	if(j["is-winter-crop"].is_bool())
		_isWinterCrop.setValue(j["is-winter-crop"].bool_value());

	if(j["is-perennial-crop"].is_bool())
		_isPerennialCrop.setValue(j["is-perennial-crop"].bool_value());

	string err;
	if(j.has_shape({{"cropParams", json11::Json::OBJECT}}, err))
	{
		auto jcps = j["cropParams"];
		if(jcps.has_shape({{"species", json11::Json::OBJECT}}, err)
			 && jcps.has_shape({{"cultivar", json11::Json::OBJECT}}, err))
			_cropParams = make_shared<CropParameters>(j["cropParams"]);
		else
			res.errors.push_back(string("Couldn't find 'species' or 'cultivar' key in JSON object 'cropParams':\n") + j.dump());

		if(_speciesName.empty() && _cropParams)
			_speciesName = _cropParams->speciesParams.pc_SpeciesId;
		if(_cultivarName.empty() && _cropParams)
			_cultivarName = _cropParams->cultivarParams.pc_CultivarId;

		if(_cropParams)
		{
			if(_isPerennialCrop.isValue())
				_cropParams->cultivarParams.pc_Perennial = _isPerennialCrop.value();
			else
				_isPerennialCrop.setValue(_cropParams->cultivarParams.pc_Perennial);
		}
	}
	else
		res.errors.push_back(string("Couldn't find 'cropParams' key in JSON object:\n") + j.dump());

	if(_isPerennialCrop.isValue() && _isPerennialCrop.value())
	{
		err = "";
		if(j.has_shape({{"perennialCropParams", json11::Json::OBJECT}}, err))
		{
			auto jcps = j["perennialCropParams"];
			if(jcps.has_shape({{"species", json11::Json::OBJECT}}, err)
				 && jcps.has_shape({{"cultivar", json11::Json::OBJECT}}, err))
				_perennialCropParams = make_shared<CropParameters>(j["cropParams"]);
		}
		else
			_perennialCropParams = _cropParams;
	}

	err = "";
	if(j.has_shape({{"residueParams", json11::Json::OBJECT}}, err))
		_residueParams = make_shared<CropResidueParameters>(j["residueParams"]);
	else
		res.errors.push_back(string("Couldn't find 'residueParams' key in JSON object:\n") + j.dump());

	err = "";
	if(j.has_shape({{"cuttingDates", json11::Json::ARRAY}}, err))
	{
		_cuttingDates.clear();
		for(auto cd : j["cuttingDates"].array_items())
			_cuttingDates.push_back(Tools::Date::fromIsoDateString(cd.string_value()));
	}

	return res;
}


json11::Json Crop::to_json(bool includeFullCropParameters) const
{
  J11Array cds;
  for(auto cd : _cuttingDates)
    cds.push_back(cd.toIsoDateString());

  J11Object o
	{{"type", "Crop"}
  ,{"id", _dbId}
  ,{"species", _speciesName}
  ,{"cultivar", _cultivarName}
  ,{"seedDate", _seedDate.toIsoDateString()}
  ,{"harvestDate", _harvestDate.toIsoDateString()}
	,{"cuttingDates", cds}
  ,{"automaticHarvest", _automaticHarvest}
  ,{"AutomaticHarvestParams", _automaticHarvestParams}};

	if(_isWinterCrop.isValue())
		o["is-winter-crop"] = _isWinterCrop.value();

  if(includeFullCropParameters)
  {
    if(_cropParams)
      o["cropParams"] = cropParameters()->to_json();
    if(_perennialCropParams && _cropParams != _perennialCropParams)
      o["perennialCropParams"] = perennialCropParameters()->to_json();
    if(_residueParams)
      o["residueParams"] = residueParameters()->to_json();
  }

  return o;
}

bool Crop::isWinterCrop() const
{
	if(_isWinterCrop.isValue())
		return _isWinterCrop.value();
	else if(seedDate().isValid() && harvestDate().isValid())
		return seedDate().dayOfYear() > harvestDate().dayOfYear();

	return false;
}

bool Crop::isPerennialCrop() const
{
	if(_isPerennialCrop.isValue())
		return _isPerennialCrop.value();

	return false;
}

string Crop::toString(bool detailed) const
{
  ostringstream s;
  s << "id: " << dbId()
		<< " species/cultivar: " << speciesName() << "/" << cultivarName() 
		<< " seedDate: " << seedDate().toString() 
		<< " harvestDate: "	<< harvestDate().toString();
	if(detailed)
    s << endl << "CropParameters: " << endl << cropParameters()->toString()
      << endl << "ResidueParameters: " << endl << residueParameters()->toString() << endl;
  
  return s.str();
}

//------------------------------------------------------------------------------

