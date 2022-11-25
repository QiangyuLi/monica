/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
Authors: 
Michael Berg <michael.berg@zalf.de>

Maintainers: 
Currently maintained by the authors.

This file is part of the MONICA model. 
Copyright (C) Leibniz Centre for Agricultural Landscape Research (ZALF)
*/

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <tuple>

#include <kj/async-io.h>

#include <capnp/message.h>

#include "json11/json11.hpp"

#include "tools/helper.h"
#include "tools/debug.h"
#include "../run/run-monica.h"
#include "json11/json11-helper.h"
#include "env-from-json-config.h"
#include "tools/algorithms.h"
#include "../io/csv-format.h"
#include "common/rpc-connections.h"
//#include "db/abstract-db-connections.h"

using namespace std;
using namespace monica;
using namespace Tools;
using namespace json11;

string appName = "monica-run";
string version = "2.0.0-beta";

int main(int argc, char** argv)
{
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");

	mas::infrastructure::common::ConnectionManager conMan;
  	auto ioContext = kj::setupAsyncIo();

	bool debug = false, debugSet = false;
	string startDate, endDate;
	string pathToOutput;
	string pathToOutputFile, pathToOutputFile2;
	bool writeOutputFile = false, writeOutputFile2 = false;
	string pathToSimJson = "./sim.json", crop, site, climate;
	string icReaderSr = "";
	string icWriterSr = "";
	
	auto printHelp = [=]()
	{
		cout
			<< appName << " [options] path-to-sim-json" << endl
			<< endl
			<< "options:" << endl 
			<< endl
			<< " -h   | --help ... this help output" << endl
			<< " -v   | --version ... outputs " << appName << " version" << endl
			<< endl
			<< " -d   | --debug ... show debug outputs" << endl
			<< " -sd  | --start-date ISO-DATE (default: start of given climate data) ... date in iso-date-format yyyy-mm-dd" << endl
			<< " -ed  | --end-date ISO-DATE (default: end of given climate data) ... date in iso-date-format yyyy-mm-dd" << endl
			<< " -w   | --write-output-files ... write MONICA output files" << endl
			<< " -op  | --path-to-output DIRECTORY (default: .) ... path to output directory" << endl
			<< " -o   | --path-to-output-file FILE ... path to output file" << endl
			<< " -c   | --path-to-crop FILE (default: ./crop.json) ... path to crop.json file" << endl
			<< " -s   | --path-to-site FILE (default: ./site.json) ... path to site.json file" << endl
			<< " -w   | --path-to-climate FILE (default: ./climate.csv) ... path to climate.csv" << endl;
	};
	
	if(argc > 1)
	{
		for (auto i = 1; i < argc; i++)
		{
			string arg = argv[i];
			if (arg == "-d" || arg == "--debug")
				debug = debugSet = true;
			else if ((arg == "-sd" || arg == "--start-date") && i + 1 < argc)
				startDate = argv[++i];
			else if ((arg == "-ed" || arg == "--end-date") && i + 1 < argc)
				endDate = argv[++i];
			else if ((arg == "-op" || arg == "--path-to-output") && i + 1 < argc)
				pathToOutput = argv[++i];
			else if ((arg == "-o" || arg == "--path-to-output-file") && i + 1 < argc)
				pathToOutputFile = argv[++i];
			else if ((arg == "-o2" || arg == "--path-to-output-file2") && i + 1 < argc)
				pathToOutputFile2 = argv[++i];
			else if ((arg == "-c" || arg == "--path-to-crop") && i + 1 < argc)
				crop = argv[++i];
			else if ((arg == "-s" || arg == "--path-to-site") && i + 1 < argc)
				site = argv[++i];
			else if ((arg == "-w" || arg == "--path-to-climate") && i + 1 < argc)
				climate = argv[++i];
			else if (arg == "-h" || arg == "--help")
				printHelp(), exit(0);
			else if (arg == "-v" || arg == "--version")
				cout << appName << " version " << version << endl, exit(0);
			else if ((arg == "-icrsr" || arg == "--intercropping-reader-sr") && i + 1 < argc)
				icReaderSr = argv[++i];
			else if ((arg == "-icwsr" || arg == "--intercropping-writer-sr") && i + 1 < argc)
				icWriterSr = argv[++i];
			else
				pathToSimJson = argv[i];
		}

		string pathOfSimJson, simFileName;
		tie(pathOfSimJson, simFileName) = splitPathToFile(pathToSimJson);

		auto simj = readAndParseJsonFile(pathToSimJson);
		if(simj.failure()) for(auto e : simj.errors) cerr << e << endl;
		auto simm = simj.result.object_items();

		auto csvos = simm["climate.csv-options"].object_items();
		if (!startDate.empty()) csvos["start-date"] = startDate;
		if (!endDate.empty()) csvos["end-date"] = endDate;
		simm["climate.csv-options"] = csvos;
		
		if(debugSet)
			simm["debug?"] = debug;

		//set debug mode in run-monica ... in libmonica has to be set separately in runMonica
		activateDebug = simm["debug?"].bool_value();

		if(!pathToOutput.empty())
			simm["path-to-output"] = pathToOutput;

		//if(!pathToOutputFile.empty())
		//	simm["path-to-output-file"] = pathToOutputFile;

		simm["sim.json"] = pathToSimJson;

		if(!crop.empty())
			simm["crop.json"] = crop;
		auto pathToCropJson = simm["crop.json"].string_value();
		if(!isAbsolutePath(pathToCropJson))
			simm["crop.json"] = pathOfSimJson + pathToCropJson;

		if(!site.empty())
			simm["site.json"] = site;
		auto pathToSiteJson = simm["site.json"].string_value();
		if(!isAbsolutePath(pathToSiteJson))
			simm["site.json"] = pathOfSimJson + pathToSiteJson;

		if(!climate.empty())
			simm["climate.csv"] = climate;
		if(simm["climate.csv"].is_string())
		{
			auto pathToClimateCSV = simm["climate.csv"].string_value();
			if(!isAbsolutePath(pathToClimateCSV))
				simm["climate.csv"] = pathOfSimJson + pathToClimateCSV;
		}
		else if(simm["climate.csv"].is_array())
		{
			vector<string> ps;
			for(auto j : simm["climate.csv"].array_items())
			{
				auto pathToClimateCSV = j.string_value();
				if(!isAbsolutePath(pathToClimateCSV))
					ps.push_back(pathOfSimJson + pathToClimateCSV);
			}
			simm["climate.csv"] = toPrimJsonArray(ps);
		}

		map<string, string> ps;
		ps["sim-json-str"] = json11::Json(simm).dump();
		ps["crop-json-str"] = printPossibleErrors(readFile(simm["crop.json"].string_value()), activateDebug);
		ps["site-json-str"] = printPossibleErrors(readFile(simm["site.json"].string_value()), activateDebug);
		//ps["path-to-climate-csv"] = simm["climate.csv"].string_value();

		auto env = createEnvFromJsonConfigFiles(ps);
		if(icReaderSr.empty()) icReaderSr = env.params.userCropParameters.pc_intercropping_reader_sr;
		if(icWriterSr.empty()) icWriterSr = env.params.userCropParameters.pc_intercropping_writer_sr;

		if(!icReaderSr.empty()) env.ic.reader = conMan.tryConnectB(ioContext, icReaderSr).castAs<Intercropping::Reader>();
		if(!icWriterSr.empty()) env.ic.writer = conMan.tryConnectB(ioContext, icWriterSr).castAs<Intercropping::Writer>();
		if(!icReaderSr.empty() && !icWriterSr.empty()) env.ic.ioContext = &ioContext;

		env.params.userSoilMoistureParameters.getCapillaryRiseRate =
			[](string soilTexture, size_t distance) {
			return Soil::readCapillaryRiseRates().getRate(soilTexture, distance);
		};

		if(activateDebug)
			cout << "starting MONICA with JSON input files" << endl;

		bool isIC = env.params.userCropParameters.isIntercropping;
		Output output, output2;
		tie(output, output2) = runMonicaIC(env, isIC);

		if(pathToOutputFile.empty() && simm["output"]["write-file?"].bool_value())
			pathToOutputFile = fixSystemSeparator(simm["output"]["path-to-output"].string_value() + "/"
								+ simm["output"]["file-name"].string_value());
		if(pathToOutputFile2.empty() && simm["output"]["write-file?"].bool_value())
			pathToOutputFile2 = fixSystemSeparator(simm["output"]["path-to-output"].string_value() + "/"
								+ simm["output"]["file-name2"].string_value());

		writeOutputFile = !pathToOutputFile.empty();
		ofstream fout;
		if(writeOutputFile)
		{
			string path, filename;
			tie(path, filename) = splitPathToFile(pathToOutputFile);
			if (!Tools::ensureDirExists(path))
			{
				cerr << "Error failed to create path: '" << path << "'." << endl;
			}
			fout.open(pathToOutputFile);
			if(fout.fail())
			{
				cerr << "Error while opening output file \"" << pathToOutputFile << "\"" << endl;
				writeOutputFile = false;
			}
		}

		ostream& out = writeOutputFile ? fout : cout;

		string csvSep = simm["output"]["csv-options"]["csv-separator"].string_value();
		bool includeHeaderRow = simm["output"]["csv-options"]["include-header-row"].bool_value();
		bool includeUnitsRow = simm["output"]["csv-options"]["include-units-row"].bool_value();
		bool includeAggRows = simm["output"]["csv-options"]["include-aggregation-rows"].bool_value();

		for(const auto& d : output.data)
		{
			out << "\"" << replace(d.origSpec, "\"", "") << "\"" << endl;
			writeOutputHeaderRows(out, d.outputIds, csvSep, includeHeaderRow, includeUnitsRow, includeAggRows);
			if(env.returnObjOutputs())
				writeOutputObj(out, d.outputIds, d.resultsObj, csvSep);
			else
				writeOutput(out, d.outputIds, d.results, csvSep);
			out << endl;
			
		}

		if(writeOutputFile)
			fout.close();

		if(isIC && !env.ic.isAsync()){
			writeOutputFile2 = !pathToOutputFile2.empty();
			ofstream fout;
			if(writeOutputFile2)
			{
				string path, filename;
				tie(path, filename) = splitPathToFile(pathToOutputFile2);
				if (!Tools::ensureDirExists(path))
				{
					cerr << "Error failed to create path: '" << path << "'." << endl;
				}
				fout.open(pathToOutputFile2);
				if(fout.fail())
				{
					cerr << "Error while opening output file \"" << pathToOutputFile2 << "\"" << endl;
					writeOutputFile2 = false;
				}
			}

			ostream& out = writeOutputFile2 ? fout : cout;

			string csvSep = simm["output"]["csv-options"]["csv-separator"].string_value();
			bool includeHeaderRow = simm["output"]["csv-options"]["include-header-row"].bool_value();
			bool includeUnitsRow = simm["output"]["csv-options"]["include-units-row"].bool_value();
			bool includeAggRows = simm["output"]["csv-options"]["include-aggregation-rows"].bool_value();

			for(const auto& d : output2.data)
			{
				out << "\"" << replace(d.origSpec, "\"", "") << "\"" << endl;
				writeOutputHeaderRows(out, d.outputIds, csvSep, includeHeaderRow, includeUnitsRow, includeAggRows);
				if(env.returnObjOutputs())
					writeOutputObj(out, d.outputIds, d.resultsObj, csvSep);
				else
					writeOutput(out, d.outputIds, d.results, csvSep);
				out << endl;
				
			}

			if(writeOutputFile2)
				fout.close();
		}

		if(activateDebug)
			cout << "finished MONICA" << endl;
	}
	else 
		printHelp();

	
	return 0;
}
