/*-------------------------------------------------------------------------------------------
 * QuickBlocks - Decentralized, useful, and detailed data from Ethereum blockchains
 * Copyright (c) 2018 Great Hill Corporation (http://quickblocks.io)
 *
 * This program is free software: you may redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version. This program is
 * distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details. You should have received a copy of the GNU General
 * Public License along with this program. If not, see http://www.gnu.org/licenses/.
 *-------------------------------------------------------------------------------------------*/
#include "etherlib.h"
#include "options.h"

//-----------------------------------------------------------------------
extern void addDefaultFuncs(CFunctionArray& funcs);
extern string_q getAssign(const CParameter *p, uint64_t which);
extern string_q getEventAssign(const CParameter *p, uint64_t which, uint64_t prevIdxs);

//-----------------------------------------------------------------------
extern const char* STR_FACTORY1;
extern const char* STR_FACTORY2;
extern const char* STR_CLASSDEF;
extern const char* STR_HEADERFILE;
extern const char* STR_HEADER_SIGS;
extern const char* STR_CODE_SIGS;
extern const char* STR_BLOCK_PATH;
extern const char* STR_ITEMS;
extern const char* STR_FORMAT_FUNCDATA;

//-----------------------------------------------------------------------
string_q templateFolder = configPath("grabABI/");

//-----------------------------------------------------------------------
int sortFunctionByName(const void *v1, const void *v2) {
    const CFunction *f1 = reinterpret_cast<const CFunction*>(v1);
    const CFunction *f2 = reinterpret_cast<const CFunction*>(v2);
    return f1->name.compare(f2->name);
}

string_q classDir;
//-----------------------------------------------------------------------
inline string_q projectName(void) {
    CFilename fn(classDir+"tmp");
    string_q ret = fn.getPath().Substitute("parselib/","").Substitute("parseLib/","").Substitute("//","");
    nextTokenClearReverse(ret,'/');
    ret = nextTokenClearReverse(ret,'/');
    return ret;
}

//-----------------------------------------------------------------------
inline void makeTheCode(const string_q& fn, const string_q& addr) {
    string_q theCode = asciiFileToString(templateFolder + fn);
    replaceAll(theCode, "[{ADDR}]", addr);
    replaceAll(theCode, "[{PROJECT_NAME}]", projectName());
    writeTheCode(classDir + "../" + fn, theCode, "", fn != "CMakeFile.txt");
}

//-----------------------------------------------------------------------
void addIfUnique(const string_q& addr, CFunctionArray& functions, CFunction& func, bool decorateNames)
{
    if (func.name.empty()) // && func.type != "constructor")
        return;

    for (uint32_t i = 0 ; i < functions.getCount() ; i++) {
        if (functions[i].encoding == func.encoding)
            return;

        // different encoding same name means a duplicate function name in the code. We won't build with
        // duplicate function names, so we need to modify the incoming function. We do this by appending
        // the first four characters of the contract's address.
        if (decorateNames && functions[i].name == func.name) {
            func.origName = func.name;
            func.name += (startsWith(addr, "0x") ? addr.substr(2,4) : addr.substr(0,4));
        }
    }

    functions[functions.getCount()] = func;
}

//-----------------------------------------------------------------------
string_q acquireABI(CFunctionArray& functions, const SFAddress& addr, const COptions& opt, bool builtIn) {

    string_q results, ret;
    string_q fileName = blockCachePath("abis/" + addr + ".json");
    string_q localFile("./" + addr + ".json");
    if (fileExists(localFile))
        copyFile(localFile, fileName);

    string_q dispName = fileName.Substitute(configPath(""),"|");
    nextTokenClear(dispName, '|');
    dispName = "~/.quickBlocks/" + dispName;
    if (fileExists(fileName) && !opt.raw) {

        if (verbose && !isTestMode()) {
            cerr << "Reading ABI for " << addr << " from cache " + dispName + "\r";
            cerr.flush();
        }
        results = asciiFileToString(fileName);

    } else {
        if (verbose && !isTestMode()) {
            cerr << "Reading ABI for " << addr << " from EtherScan\r";
            cerr.flush();
        }
        string_q url = string_q("http:/")
                            + "/api.etherscan.io/api?module=contract&action=getabi&address="
                            + addr;
        results = urlToString(url).Substitute("\\", "");
        if (!contains(results, "NOTOK")) {
        	// clear the RPC wrapper
        	replace(results, "{\"status\":\"1\",\"message\":\"OK\",\"result\":\"","");
        	replaceReverse(results, "]\"}", "");
        	if (verbose) {
            	if (!isTestMode())
                	cout << verbose << "---------->" << results << "\n";
            	cout.flush();
        	}
            nextTokenClear(results, '[');
            replaceReverse(results, "]}", "");
            if (!isTestMode()) {
                cerr << "Caching abi in " << dispName << "\n";
            }
            establishFolder(fileName);
            stringToAsciiFile(fileName, "["+results+"]");
        } else {
            if (!opt.silent) {
                cerr << "Etherscan returned " << results << "\n";
                cerr << "Could not grab ABI for " + addr + " from etherscan.io.\n";
                exit(0);
            }
            // TODO: If we store the ABI here even if empty, we won't have to get it again, but then what happens
            // if user later posts the ABI? Need a 'refresh' option or clear cache option
            establishFolder(fileName);
            stringToAsciiFile(fileName, "[]");
        }
    }

    ret = results.Substitute("\n", "").Substitute("\t", "").Substitute(" ", "");
    char *s = (char *)(results.c_str()); // NOLINT
    char *p = cleanUpJson(s);
    while (p && *p) {
        CFunction func;
        uint32_t nFields = 0;
        p = func.parseJson(p, nFields);
        func.isBuiltin = builtIn;
        addIfUnique(addr, functions, func, opt.decNames);
    }

    return ret;
}

//-----------------------------------------------------------------------
int main(int argc, const char *argv[]) {

    etherlib_init();

    // We keep only a single slurper. If the user is using the --file option and they
    // are reading the same account repeatedly, we only need to read the cache once.
    COptions options;

    if (!options.prepareArguments(argc, argv))
        return 0;

    while (!options.commandList.empty()) {
        string_q command = nextTokenClear(options.commandList, '\n');
        if (!options.parseArguments(command))
            return 0;

        if (options.open) {
            for (uint64_t i = 0 ; i < options.nAddrs ; i++) {
                string_q fileName = blockCachePath("abis/" + options.addrs[i] + ".json");
                if (!fileExists(fileName)) {
                    cerr << "ABI for '" + options.addrs[i] + "' not found. Quitting...\n";
                    return 0;
                }
                editFile(fileName);
            }
            return 0;
        }

        if (options.asJson) {
            for (uint64_t i = 0 ; i < options.nAddrs ; i++) {
                string_q fileName = blockCachePath("abis/" + options.addrs[i] + ".json");
                if (!fileExists(fileName)) {
                    cerr << "ABI for '" + options.addrs[i] + "' not found. Quitting...\n";
                    return 0;
                }
                cout << asciiFileToString(fileName) << "\n";
            }
            return 0;
        }

        CFunctionArray functions;
        string_q addrList;
        bool isGenerate = !options.classDir.empty();
        if (!(options.addrs[0] % "0xTokenLib") && !(options.addrs[0] % "0xWalletLib") && isGenerate)
        {
            acquireABI(functions, "0xTokenLib",  options, true);
            acquireABI(functions, "0xWalletLib", options, true);
        }
        for (uint64_t i = 0 ; i < options.nAddrs ; i++) {
            options.theABI += ("ABI for addr : " + options.addrs[i] + "\n");
            options.theABI += acquireABI(functions, options.addrs[i], options, false) + "\n\n";
            addrList += (options.addrs[i] + "|");
        }

        if (!isGenerate) {

            if (options.asData) {
                for (uint32_t i = 0 ; i < functions.getCount() ; i++) {
                    CFunction *func = &functions[i];
                    if (!func->constant || !options.noconst) {
                        string_q format = getGlobalConfig()->getDisplayStr(false, STR_FORMAT_FUNCDATA);
                        cout << func->Format(format);
                    }
                }

            } else {
                // print to a buffer because we have to modify it before we print it
                cout << "ABI for address " << options.primaryAddr << (options.nAddrs>1 ? " and others" : "") << "\n";
                for (uint32_t i = 0 ; i < functions.getCount() ; i++) {
                    CFunction *func = &functions[i];
                    if (!func->constant || !options.noconst)
                        cout << func->getSignature(options.parts) << "\n";
                }
                cout << "\n";
            }

        } else {
            verbose = false;
            functions.Sort(sortFunctionByName);
//            if (options.isToken())
//                addDefaultFuncs(functions);

            classDir = (options.classDir).Substitute("~/", getHomeFolder());
            string_q classDefs = classDir + "classDefinitions/";
            establishFolder(classDefs);

            string_q funcExterns, evtExterns, funcDecls, evtDecls, sigs, evts;
            string_q headers;
            if (!options.isToken()) headers += ("#include \"tokenlib.h\"\n");
            if (!options.isWallet()) headers += ("#ifndef NOWALLETLIB\n#include \"walletlib.h\"\n#endif\n");
            string_q sources = "src= \\\n", registers, factory1, factory2;
            for (uint32_t i = 0 ; i < functions.getCount() ; i++) {
                CFunction *func = &functions[i];
                if (!func->isBuiltin) {
                    string_q name = func->Format("[{NAME}]") + (func->type == "event" ? "Event" : "");
                    if (name == "eventEvent")
                        name = "logEntry";
                    if (startsWith(name, '_'))
                        name = name.substr(1);
                    char ch = static_cast<char>(toupper(name[0]));
                    string_q fixed(ch);
                    name = fixed + name.substr(1);
                    string_q theClass = (options.isBuiltin() ? "Q" : "C") + name;
                    bool isConst = func->constant;
                    bool isEmpty = name.empty() || func->type.empty();
                    bool isLog = contains(toLower(name), "logentry");
//                    bool isConstructor = func->type % "constructor";
//                    if (!isConst && !isEmpty && !isLog) { // && !isConstructor) {
                    if (!isEmpty && !isLog) {
                        if (name != "DefFunction") {
                            if (func->type == "event") {
                                evtExterns += func->Format("extern const string_q evt_[{NAME}]{QB};\n");
                                string_q decl = "const string_q evt_[{NAME}]{QB} = \"" + func->encoding + "\";\n";
                                evtDecls += func->Format(decl);
                                if (!options.isBuiltin())
                                    evts += func->Format("\tevt_[{NAME}],\n");
                            } else {
                                funcExterns += func->Format("extern const string_q func_[{NAME}]{QB};\n");
                                string_q decl = "const string_q func_[{NAME}]{QB} = \"" + func->encoding + "\";\n";
                                funcDecls += func->Format(decl);
                                if (!options.isBuiltin())
                                    sigs += func->Format("\tfunc_[{NAME}],\n");
                            }
                        }
                        string_q fields, assigns1, assigns2, items1;
                        uint64_t nIndexed = 0;
                        for (uint32_t j = 0 ; j < func->inputs.getCount() ; j++) {
                            fields   += func->inputs[j].Format("[{TYPE}][ {NAME}]|");
                            assigns1 += func->inputs[j].Format(getAssign(&func->inputs[j], j));
                            items1   += "\t\t\titems[nItems++] = \"" + func->inputs[j].type + "\";\n";
                            nIndexed += func->inputs[j].indexed;
                            string_q res = func->inputs[j].Format(getEventAssign(&func->inputs[j], j+1, nIndexed));
                            replace(res, "++", "[");
                            replace(res, "++", "]");
                            assigns2 += res;
                        }

                        string_q base = (func->type == "event" ? "LogEntry" : "Transaction");
                        if (name == "LogEntry")
                            base = "LogEntry";

                        string_q out = STR_CLASSDEF;
                        replace(out, "[{DIR}]", options.classDir.Substitute(getHomeFolder(), "~/"));
                        replace(out, "[{CLASS}]", theClass);
                        replace(out, "[{FIELDS}]", fields);
                        replace(out, "[{BASE}]", base);
                        replace(out, "[{BASE_LOWER}]", toLower(base));

                        string_q fileName = toLower(name)+".txt";
                        if (!isConst) {
                            headers += ("#include \"" + fileName.Substitute(".txt", ".h") + "\"\n");
                            registers += "\t" + theClass + "::registerClass();\n";
                        }
                        sources += fileName.Substitute(".txt", ".cpp") + " \\\n";
                        if (base == "Transaction") {
                            string_q f1, fName = func->Format("[{NAME}]");
                            f1 = string_q(STR_FACTORY1);
                            replaceAll(f1, "[{CLASS}]", theClass);
                            replaceAll(f1, "[{NAME}]", fName);
                            if (fName == "defFunction")
                                replaceAll(f1, "encoding == func_[{LOWER}]", "encoding.length() < 10");
                            else
                                replaceAll(f1, "[{LOWER}]", fName);
                            replaceAll(f1, "[{ASSIGNS1}]", assigns1);
                            replaceAll(f1, "[{ITEMS1}]", items1);
                            string_q parseIt = "toFunction(\"" + fName + "\", params, nItems, items)";
                            replaceAll(f1, "[{PARSEIT}]", parseIt);
                            replaceAll(f1, "[{BASE}]", base);
                            replaceAll(f1, "[{SIGNATURE}]", func->getSignature(SIG_DEFAULT)
                                                            .Substitute("\t", "")
                                                            .Substitute("  ", " ")
                                                            .Substitute(" (", "(")
                                                            .Substitute(",", ", "));
                            replace(f1, "[{ENCODING}]", func->getSignature(SIG_ENCODE));
                            replace(f1, " defFunction(string)", "()");
                            if (!isConst)
                                factory1 += f1;

                        } else if (name != "LogEntry") {
                            string_q f2, fName = func->Format("[{NAME}]");
                            f2 = string_q(STR_FACTORY2)
                                            .Substitute("[{CLASS}]", theClass)
                                            .Substitute("[{LOWER}]", fName);
                            replace(f2, "[{ASSIGNS2}]", assigns2);
                            replace(f2, "[{BASE}]", base);
                            replace(f2, "[{SIGNATURE}]", func->getSignature(SIG_DEFAULT|SIG_IINDEXED)
                                       .Substitute("\t", "").Substitute("  ", " ")
                                       .Substitute(" (", "(").Substitute(",", ", "));
                            replace(f2, "[{ENCODING}]", func->getSignature(SIG_ENCODE));
                            if (!isConst)
                                factory2 += f2;
                        }

                        if (name != "logEntry" && !isConst) {
                            // hack warning
                            replaceAll(out, "bytes32[]", "CStringArray");
                            replaceAll(out, "uint256[]", "SFBigUintArray");  // order matters
                            replaceAll(out, "int256[]", "SFBigIntArray");
                            replaceAll(out, "uint32[]", "SFUintArray");  // order matters
                            replaceAll(out, "int32[]", "SFIntArray");
                            stringToAsciiFile(classDefs+fileName, out);
                            if (func->type == "event")
                                cout << "Generating class for event type: '" << theClass << "'\n";
                            else
                                cout << "Generating class for derived transaction type: '" << theClass << "'\n";

                            string_q makeClass = configPath("makeClass/makeClass");
                            if (!fileExists(makeClass)) {
                                cerr << makeClass << " was not found. This executable is required to run grabABI. Quitting...\n";
                                exit(0);
                            }
                            string_q res = doCommand(makeClass + " -r " + toLower(name));
                            if (!res.empty())
                                cout << "\t" << res << "\n";
                        }
                    }
                }
            }

            // The library header file
            if (!options.isBuiltin())
                headers += ("#include \"processing.h\"\n");
            string_q headerCode = string_q(STR_HEADERFILE).Substitute("[{HEADERS}]", headers);
            string_q parseInit = "parselib_init(QUITHANDLER qh=defaultQuitHandler)";
            if (!options.isBuiltin())
                replaceAll(headerCode, "[{PREFIX}]_init(void)", parseInit);
            replaceAll(headerCode, "[{ADDR}]", options.primaryAddr.Substitute("0x", ""));
            replaceAll(headerCode, "[{HEADER_SIGS}]", options.isBuiltin() ? "" : STR_HEADER_SIGS);
            replaceAll(headerCode, "[{PREFIX}]", toLower(options.prefix));
            string_q pprefix = (options.isBuiltin() ? toProper(options.prefix).Substitute("lib", "") : "Func");
            replaceAll(headerCode, "[{PPREFIX}]", pprefix);
            replaceAll(headerCode, "FuncEvent", "Event");
            string_q comment = "//------------------------------------------------------------------------\n";
            funcExterns = (funcExterns.empty() ? "// No functions" : funcExterns);
            evtExterns = (evtExterns.empty() ? "// No events" : evtExterns);
            replaceAll(headerCode, "[{EXTERNS}]", comment+funcExterns+"\n"+comment+evtExterns);
            headerCode = headerCode.Substitute("{QB}", (options.isBuiltin() ? "_qb" : ""));
            writeTheCode(classDir + options.prefix + ".h", headerCode);

            // The library make file
            replaceReverse(sources, " \\\n", " \\\n" + options.prefix + ".cpp\n");
            if (!options.isBuiltin()) {
                string_q makefile = asciiFileToString(templateFolder + "parselib/CMakeLists.txt");
                replaceAll(makefile, "[{PROJECT_NAME}]", projectName());
                writeTheCode(classDir + "CMakeLists.txt", makefile);
            }

            // The library source file
            replace(factory1, "} else ", "");
            replace(factory1, "contains(func, \"defFunction|\")", "!contains(func, \"|\")");
            replace(factory1, " if (encoding == func_[{LOWER}])", "");
            replace(factory2, "} else ", "");

            string_q sourceCode = asciiFileToString(templateFolder + "parselib/parselib.cpp");
            parseInit = "parselib_init(QUITHANDLER qh)";
            if (!options.isBuiltin())
                replaceAll(sourceCode, "[{PREFIX}]_init(void)", parseInit);
            if (options.isToken()) {
                replace(sourceCode, "return promoteToToken(p);", "return promoteToWallet(p);");
                replace(sourceCode, "return promoteToTokenEvent(p);", "return promoteToWalletEvent(p);");
            } else if (options.isWallet()) {
                replace(sourceCode, "return promoteToToken(p);", "return NULL;");
                replace(sourceCode, "return promoteToTokenEvent(p);", "return NULL;");
            }
            replace(sourceCode, "[{BLKPATH}]", options.isBuiltin() ? "" : STR_BLOCK_PATH);
            replaceAll(sourceCode, "[{CODE_SIGS}]", (options.isBuiltin() ? "" : STR_CODE_SIGS));
            replaceAll(sourceCode, "[{ADDR}]", options.primaryAddr.Substitute("0x", ""));
            replaceAll(sourceCode, "[{ABI}]", options.theABI);
            replaceAll(sourceCode, "[{REGISTERS}]", registers);
            string_q chainInit = (options.isToken() ?
                                    "\twalletlib_init();\n" :
                                  (options.isWallet() ? "" : "\ttokenlib_init();\n"));
            replaceAll(sourceCode, "[{CHAINLIB}]",  chainInit);
            replaceAll(sourceCode, "[{FACTORY1}]",  factory1.empty() ? "\t\t{\n\t\t\t// No functions\n" : factory1);
            replaceAll(sourceCode, "[{INIT_CODE}]", factory1.empty() ? "" : STR_ITEMS);
            replaceAll(sourceCode, "[{FACTORY2}]",  factory2.empty() ? "\t\t{\n\t\t\t// No events\n" : factory2);

            headers = ("#include \"tokenlib.h\"\n");
            headers += ("#include \"walletlib.h\"\n");
            if (!options.isBuiltin())
                headers += "#include \"[{PREFIX}].h\"\n";
            replaceAll(sourceCode, "[{HEADERS}]", headers);
            replaceAll(sourceCode, "[{PREFIX}]", options.prefix);
            pprefix = (options.isBuiltin() ? toProper(options.prefix).Substitute("lib", "") : "Func");
            replaceAll(sourceCode, "[{PPREFIX}]", pprefix);
            replaceAll(sourceCode, "FuncEvent", "Event");
            replaceAll(sourceCode, "[{FUNC_DECLS}]", funcDecls.empty() ? "// No functions" : funcDecls);
            replaceAll(sourceCode, "[{SIGS}]", sigs.empty() ? "\t// No functions\n" : sigs);
            replaceAll(sourceCode, "[{EVENT_DECLS}]", evtDecls.empty() ? "// No events" : evtDecls);
            replaceAll(sourceCode, "[{EVTS}]", evts.empty() ? "\t// No events\n" : evts);
            sourceCode = sourceCode.Substitute("{QB}", (options.isBuiltin() ? "_qb" : ""));
            writeTheCode(classDir + options.prefix + ".cpp", sourceCode.Substitute("XXXX","[").Substitute("YYYY","]"));

            // The code
            if (!options.isBuiltin()) {
                makeTheCode("rebuild",        trimTrailing(addrList,'|').Substitute("|", " "));
                makeTheCode("CMakeLists.txt", options.primaryAddr);
                makeTheCode("debug.h",        options.primaryAddr);
                makeTheCode("debug.cpp",      options.primaryAddr);
                makeTheCode("options.cpp",    options.primaryAddr);
                makeTheCode("options.h",      options.primaryAddr);
                makeTheCode("main.cpp",       options.primaryAddr);
                makeTheCode("visitor.cpp",    options.primaryAddr);
                makeTheCode("visitor.h",      options.primaryAddr);
                makeTheCode("accounting.cpp", options.primaryAddr);
                makeTheCode("display.cpp",    options.primaryAddr);
                makeTheCode("processing.cpp", options.primaryAddr);
                makeTheCode("processing.h",   options.primaryAddr);

            }
        }
    }
    return 0;
}

//-----------------------------------------------------------------------
string_q getAssign(const CParameter *p, uint64_t which) {

    string_q ass;
    string_q type = p->Format("[{TYPE}]");

    if (contains(type, "[") && contains(type, "]")) {
        const char* STR_ASSIGNARRAY =
            "\t\t\twhile (!params.empty()) {\n"
            "\t\t\t\tstring_q val = params.substr(0,64);\n"
            "\t\t\t\tparams = params.substr(64);\n"
            "\t\t\t\ta->[{NAME}]XXXXa->[{NAME}].getCount()YYYY = val;\n"
            "\t\t\t}\n";
        return p->Format(STR_ASSIGNARRAY);
    }

    if (type == "uint" || type == "uint256") { ass = "toWei(\"0x\"+[{VAL}]);";
    } else if (contains(type, "gas")) { ass = "toGas([{VAL}]);";
    } else if (contains(type, "uint64")) { ass = "toLongU([{VAL}]);";
    } else if (contains(type, "uint")) { ass = "toLong32u([{VAL}]);";
    } else if (contains(type, "int") || contains(type, "bool")) { ass = "toLong([{VAL}]);";
    } else if (contains(type, "address")) { ass = "toAddress([{VAL}]);";
    } else { ass = "[{VAL}];";
    }

    replace(ass, "[{VAL}]", "params.substr(" + asStringU(which) + "*64" + (type == "bytes" ? "" : ",64") + ")");
    return p->Format("\t\t\ta->[{NAME}] = " + ass + "\n");
}

//-----------------------------------------------------------------------
string_q getEventAssign(const CParameter *p, uint64_t which, uint64_t nIndexed) {
    string_q type = p->Format("[{TYPE}]"), ass;
    if (type == "uint" || type == "uint256") { ass = "toWei([{VAL}]);";
    } else if (contains(type, "gas")) { ass = "toGas([{VAL}]);";
    } else if (contains(type, "uint64")) { ass = "toLongU([{VAL}]);";
    } else if (contains(type, "uint")) { ass = "toLong32u([{VAL}]);";
    } else if (contains(type, "int") || contains(type, "bool")) { ass = "toLong([{VAL}]);";
    } else if (contains(type, "address")) { ass = "toAddress([{VAL}]);";
    } else { ass = "[{VAL}];";
    }

    if (p->indexed) {
        replace(ass, "[{VAL}]", "nTops > [{WHICH}] ? fromTopic(p->topics[{IDX}]) : \"\"");

    } else if (type == "bytes") {
        replace(ass, "[{VAL}]", "\"0x\"+data.substr([{WHICH}]*64)");
        which -= (nIndexed+1);

    } else {
        replace(ass, "[{VAL}]", string_q(type == "address" ? "" : "\"0x\"+") + "data.substr([{WHICH}]*64,64)");
        which -= (nIndexed+1);
    }
    replace(ass, "[{IDX}]", "++" + asStringU(which)+"++");
    replace(ass, "[{WHICH}]", asStringU(which));
    string_q fmt = "\t\t\ta->[{NAME}] = " + ass + "\n";
    return p->Format(fmt);
}

//-----------------------------------------------------------------------
//void addDefaultFuncs(CFunctionArray& funcs) {
//    CFunction func;
//    func.constant = false;
//    //  func.type = "function";
//    //  func.inputs[0].type = "string";
//    //  func.inputs[0].name = "_str";
//    //  func.name = "defFunction";
//    //  funcs[funcs.getCount()] = func;
//    func.type = "event";
//    func.name = "event";
//    func.inputs.Clear();
//    funcs[funcs.getCount()] = func;
//}

//-----------------------------------------------------------------------
const char* STR_FACTORY1 =
"\t\t} else if (encoding == func_[{LOWER}]{QB})\n"
"\t\t{\n"
"\t\t\t// [{SIGNATURE}]\n"
"\t\t\t// [{ENCODING}]\n"
"\t\t\t[{CLASS}] *a = new [{CLASS}];\n"
"\t\t\t*(C[{BASE}]*)a = *p; // copy in\n"
"[{ASSIGNS1}]"
"[{ITEMS1}]"
"\t\t\ta->function = [{PARSEIT}];\n"
"\t\t\treturn a;\n"
"\n";

//-----------------------------------------------------------------------
const char* STR_FACTORY2 =
"\t\t} else if (fromTopic(p->topics[0]) % evt_[{LOWER}]{QB})\n"
"\t\t{\n"
"\t\t\t// [{SIGNATURE}]\n"
"\t\t\t// [{ENCODING}]\n"
"\t\t\t[{CLASS}] *a = new [{CLASS}];\n"
"\t\t\t*(C[{BASE}]*)a = *p; // copy in\n"
"[{ASSIGNS2}]"
"\t\t\treturn a;\n"
"\n";

//-----------------------------------------------------------------------
const char* STR_CLASSDEF =
"[settings]\n"
"class     = [{CLASS}]\n"
"baseClass = C[{BASE}]\n"
"fields    = [{FIELDS}]\n"
"includes  = [{BASE_LOWER}].h\n"
"cIncs     = #include \"etherlib.h\"\n";

//-----------------------------------------------------------------------
const char* STR_HEADERFILE =
"#pragma once\n"
"/*-------------------------------------------------------------------------\n"
" * This source code is confidential proprietary information which is\n"
" * Copyright (c) 2017 by Great Hill Corporation.\n"
" * All Rights Reserved\n"
" *\n"
" * The LICENSE at the root of this repo details your rights (if any)\n"
" *------------------------------------------------------------------------*/\n"
" /*\n"
"  *\n"
"  * This code was generated automatically from grabABI and makeClass You may \n"
"  * edit the file, but keep your edits inside the 'EXISTING_CODE' tags.\n"
"  *\n"
"  */\n"
"#include \"etherlib.h\"\n"
"[{HEADERS}]\n"
"//------------------------------------------------------------------------\n"
"extern void [{PREFIX}]_init(void);\n"
"extern const CTransaction *promoteTo[{PPREFIX}](const CTransaction *p);\n"
"extern const CLogEntry *promoteTo[{PPREFIX}]Event(const CLogEntry *p);\n"
"\n[{EXTERNS}][{HEADER_SIGS}]\n\n// EXISTING_CODE\n// EXISTING_CODE\n";

//-----------------------------------------------------------------------
const char* STR_HEADER_SIGS =
"\n\n//-----------------------------------------------------------------------------\n"
"extern string_q sigs[];\n"
"extern string_q topics[];\n"
"\n"
"extern uint32_t nSigs;\n"
"extern uint32_t nTopics;";

//-----------------------------------------------------------------------
const char* STR_CODE_SIGS =
"\n\n//-----------------------------------------------------------------------------\n"
"string_q sigs[] = {\n"
"\t// Token support\n"
"\tfunc_approve_qb,\n"
"\tfunc_transferFrom_qb,\n"
"\tfunc_transfer_qb,\n"
"\t// Wallet support\n"
"\tfunc_addOwner_qb,\n"
"\tfunc_changeOwner_qb,\n"
"\tfunc_changeRequirement_qb,\n"
"\tfunc_confirm_qb,\n"
"\tfunc_execute_qb,\n"
"\tfunc_isOwner_qb,\n"
"\tfunc_kill_qb,\n"
"\tfunc_removeOwner_qb,\n"
"\tfunc_resetSpentToday_qb,\n"
"\tfunc_revoke_qb,\n"
"\tfunc_setDailyLimit_qb,\n"
"\t// Contract support\n"
"[{SIGS}]"
"};\n"
"uint32_t nSigs = sizeof(sigs) / sizeof(string_q);\n"
"\n"
"//-----------------------------------------------------------------------------\n"
"string_q topics[] = {\n"
"\t// Token support\n"
"\tevt_Transfer_qb,\n"
"\tevt_Approval_qb,\n"
"\t// Wallet support\n"
"\tevt_Confirmation_qb,\n"
"\tevt_ConfirmationNeeded_qb,\n"
"\tevt_Deposit_qb,\n"
"\tevt_MultiTransact_qb,\n"
"\tevt_OwnerAdded_qb,\n"
"\tevt_OwnerChanged_qb,\n"
"\tevt_OwnerRemoved_qb,\n"
"\tevt_RequirementChanged_qb,\n"
"\tevt_Revoke_qb,\n"
"\tevt_SingleTransact_qb,\n"
"\t// Contract support\n"
"[{EVTS}]"
"};\n"
"uint32_t nTopics = sizeof(topics) / sizeof(string_q);\n"
"\n";

//-----------------------------------------------------------------------
const char* STR_BLOCK_PATH = "etherlib_init(qh);\n\n";

//-----------------------------------------------------------------------
const char* STR_ITEMS =
"\t\tstring_q items[256];\n"
"\t\tuint32_t nItems=0;\n"
"\n"
"\t\tstring_q encoding = p->input.substr(0,10);\n"
"\t\tstring_q params   = p->input.substr(10);\n";

//-----------------------------------------------------------------------
const char* STR_FORMAT_FUNCDATA =
"[{name}]\t"
"[{type}]\t"
"[{anonymous}]\t"
"[{constant}]\t"
"[{payable}]\t"
"[{signature}]\t"
"[{encoding}]\t"
"[{inputs}]\t"
"[{outputs}]\n";
