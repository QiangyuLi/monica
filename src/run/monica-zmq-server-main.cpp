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

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <tuple>

#include "zhelpers.hpp"

#include "json11/json11.hpp"

#include "tools/zmq-helper.h"
#include "tools/helper.h"

#include "db/abstract-db-connections.h"
#include "tools/debug.h"
#include "../run/run-monica.h"
#include "../run/run-monica-zmq.h"
#include "../io/database-io.h"
#include "../core/monica.h"
#include "tools/json11-helper.h"
#include "climate/climate-file-io.h"
#include "soil/conversion.h"
#include "env-from-json.h"
#include "tools/algorithms.h"
#include "../io/csv-format.h"

using namespace std;
using namespace Monica;
using namespace Tools;
using namespace json11;

string appName = "monica-zmq-server";
string version = "2.0.0-beta";

int main(int argc, char** argv)
{
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");

	//use a possibly non-default db-connections.ini
	//Db::dbConnectionParameters("db-connections.ini");

	int port = 5560;
	string address = "localhost";
	int resultPort = 7777;
	string resultAddress = "localhost";
	bool usePipeline = false;
	bool connectToZmqProxy = false;

	auto printHelp = [=]()
	{
		cout
			<< appName << endl
			<< " [-d | --debug] ... show debug outputs" << endl
			<< " [[-c | --connect-to-proxy]] ... connect MONICA server process to a ZeroMQ proxy" << endl
			<< " [[-a | --address] (PROXY-)ADDRESS (default: " << address << ")] ... connect client to give IP address" << endl
			<< " [[-p | --port] (PROXY-)PORT (default: " << port << ")] ... run server/connect client on/to given port" << endl
			<< " [[-r | --result] ... use different result socket (parameter is optional, when non default result address/port are used)" << endl
			<< " [[-ra | --result-address] ADDRESS (default: " << resultAddress << ")] ... bind socket to this IP address for results" << endl
			<< " [[-rp | --result-port] PORT (default: " << resultPort << ")] ... bind socket to this port for results" << endl
			<< " [-h | --help] ... this help output" << endl
			<< " [-v | --version] ... outputs MONICA version" << endl;
	};

	zmq::context_t context(1);

	if(argc >= 1)
	{
		for(auto i = 1; i < argc; i++)
		{
			string arg = argv[i];
			if(arg == "-d" || arg == "--debug")
				activateDebug = true;
			else if((arg == "-c" || arg == "--connect-to-proxy"))
				connectToZmqProxy = true;
			else if((arg == "-a" || arg == "--address")
							&& i + 1 < argc)
				address = argv[++i];
			else if((arg == "-p" || arg == "--port")
							&& i + 1 < argc)
				port = stoi(argv[++i]);
			if(arg == "-r" || arg == "--result-socket")
				usePipeline = true;
			else if((arg == "-ra" || arg == "--result-address")
							&& i + 1 < argc)
			{
				address = argv[++i];
				usePipeline = true;
			}
			else if((arg == "-rp" || arg == "--result-port")
							&& i + 1 < argc)
			{
				port = stoi(argv[++i]);
				usePipeline = true;
			}
			else if(arg == "-h" || arg == "--help")
				printHelp(), exit(0);
			else if(arg == "-v" || arg == "--version")
				cout << appName << " version " << version << endl, exit(0);
		}

		debug() << "starting ZeroMQ MONICA server" << endl;
		
		string recvAddress = string("tcp://") + (connectToZmqProxy ? address : "*") + ":" + to_string(port);
		vector<pair<ZmqSocketType, string>> addresses;
		if(usePipeline)
		{
			addresses.push_back(make_pair(Pull, recvAddress));
			addresses.push_back(make_pair(Push, string("tcp://*:") + to_string(port)));
		}
		if(connectToZmqProxy)
			addresses.push_back(make_pair(ProxyReply, recvAddress));
		else
			addresses.push_back(make_pair(Reply, recvAddress));

		serveZmqMonicaFull(&context, addresses);
		
		debug() << "stopped ZeroMQ MONICA server" << endl;
	}

	return 0;
}
