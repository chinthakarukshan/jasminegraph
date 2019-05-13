/**
Copyright 2019 JasminGraph Team
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
 */

#include <sstream>
#include <ctime>
#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include "JasmineGraphFrontEnd.h"
#include "../util/Conts.h"
#include "../util/Utils.h"
#include "../util/kafka/KafkaCC.h"
#include "JasmineGraphFrontEndProtocol.h"
#include "../metadb/SQLiteDBInterface.h"
#include "../partitioner/local/MetisPartitioner.h"
#include "../partitioner/local/RDFPartitioner.h"
#include "../util/logger/Logger.h"
#include "../server/JasmineGraphServer.h"
#include "../partitioner/local/RDFParser.h"
#include "../server/JasmineGraphInstanceProtocol.h"

using namespace std;

static int connFd;
Logger frontend_logger;

void *frontendservicesesion(void *dummyPt) {
    frontendservicesessionargs *sessionargs = (frontendservicesessionargs *) dummyPt;
    frontend_logger.log("Thread No: " + to_string(pthread_self()), "info");
    int connFd = sessionargs->connFd;
    char data[300];
    bzero(data, 301);
    bool loop = false;
    while (!loop) {
        bzero(data, 301);
        read(connFd, data, 300);

        string line(data);
        frontend_logger.log("Command received: " + line, "info");

        Utils utils;
        line = utils.trim_copy(line, " \f\n\r\t\v");

        if (line.compare(EXIT) == 0) {
            break;
        } else if (line.compare(LIST) == 0) {
            SQLiteDBInterface *sqlite = &sessionargs->sqlite;
            std::stringstream ss;
            std::vector<vector<pair<string, string>>> v = sqlite->runSelect(
                    "SELECT idgraph, name, upload_path FROM graph;");
            for (std::vector<vector<pair<string, string>>>::iterator i = v.begin(); i != v.end(); ++i) {
                ss << "|";
                for (std::vector<pair<string, string>>::iterator j = (i->begin()); j != i->end(); ++j) {
                    ss << j->second << "|";
                }
                ss << "\n";
            }
            string result = ss.str();
            write(connFd, result.c_str(), result.length());

        } else if (line.compare(SHTDN) == 0) {
            close(connFd);
            exit(0);
        } else if (line.compare(ADRDF) == 0) {

            // add RDF graph
            write(connFd, SEND.c_str(), FRONTEND_COMMAND_LENGTH);
            write(connFd, "\r\n", 2);

            // We get the name and the path to graph as a pair separated by |.
            char graph_data[300];
            bzero(graph_data, 301);
            string name = "";
            string path = "";

            read(connFd, graph_data, 300);

            std::time_t time = chrono::system_clock::to_time_t(chrono::system_clock::now());
            string uploadStartTime = ctime(&time);
            string gData(graph_data);

            Utils utils;
            gData = utils.trim_copy(gData, " \f\n\r\t\v");
            frontend_logger.log("Data received: " + gData, "info");

            std::vector<std::string> strArr = Utils::split(gData, '|');

            if (strArr.size() != 2) {
                frontend_logger.log("Message format not recognized", "error");
                break;
            }

            name = strArr[0];
            path = strArr[1];

            if (JasmineGraphFrontEnd::graphExists(path, dummyPt)) {
                frontend_logger.log("Graph exists", "error");
                break;
            }

            if (utils.fileExists(path)) {
                std::cout << "Path exists" << endl;

                SQLiteDBInterface *sqlite = &sessionargs->sqlite;
                string sqlStatement =
                        "INSERT INTO graph (name,upload_path,upload_start_time,upload_end_time,graph_status_idgraph_status,"
                        "vertexcount,centralpartitioncount,edgecount) VALUES(\"" + name + "\", \"" + path +
                        "\", \"" + uploadStartTime + "\", \"\",\"" + to_string(Conts::GRAPH_STATUS::LOADING) +
                        "\", \"\", \"\", \"\")";
                int newGraphID = sqlite->runInsert(sqlStatement);

                GetConfig appConfig;
                appConfig.readConfigFile(path, newGraphID);

                MetisPartitioner *metisPartitioner = new MetisPartitioner(&sessionargs->sqlite);
                string input_file_path = utils.getHomeDir() + "/.jasminegraph/tmp/" + to_string(newGraphID) + "/" +
                                         to_string(newGraphID);
                metisPartitioner->loadDataSet(input_file_path, newGraphID);

                metisPartitioner->constructMetisFormat(Conts::GRAPH_TYPE_RDF);
                metisPartitioner->partitioneWithGPMetis();
                JasmineGraphServer *jasmineServer = new JasmineGraphServer();
                jasmineServer->uploadGraphLocally(newGraphID, Conts::GRAPH_WITH_ATTRIBUTES);
                utils.deleteDirectory(utils.getHomeDir() + "/.jasminegraph/tmp/" + to_string(newGraphID));
                utils.deleteDirectory("/tmp/" + std::to_string(newGraphID));

            } else {
                frontend_logger.log("Graph data file does not exist on the specified path", "error");
                break;
            }

        } else if (line.compare(ADGR) == 0) {
            write(connFd, SEND.c_str(), FRONTEND_COMMAND_LENGTH);
            write(connFd, "\r\n", 2);

            // We get the name and the path to graph as a pair separated by |.
            char graph_data[300];
            bzero(graph_data, 301);
            string name = "";
            string path = "";

            read(connFd, graph_data, 300);

            std::time_t time = chrono::system_clock::to_time_t(chrono::system_clock::now());
            string uploadStartTime = ctime(&time);
            string gData(graph_data);

            Utils utils;
            gData = utils.trim_copy(gData, " \f\n\r\t\v");
            frontend_logger.log("Data received: " + gData, "info");

            std::vector<std::string> strArr = Utils::split(gData, '|');

            if (strArr.size() != 2) {
                frontend_logger.log("Message format not recognized", "error");
                break;
            }

            name = strArr[0];
            path = strArr[1];

            if (JasmineGraphFrontEnd::graphExists(path, dummyPt)) {
                frontend_logger.log("Graph exists", "error");
                break;
            }

            if (utils.fileExists(path)) {
                std::cout << "Path exists" << endl;

                SQLiteDBInterface *sqlite = &sessionargs->sqlite;
                string sqlStatement =
                        "INSERT INTO graph (name,upload_path,upload_start_time,upload_end_time,graph_status_idgraph_status,"
                        "vertexcount,centralpartitioncount,edgecount) VALUES(\"" + name + "\", \"" + path +
                        "\", \"" + uploadStartTime + "\", \"\",\"" + to_string(Conts::GRAPH_STATUS::LOADING) +
                        "\", \"\", \"\", \"\")";
                int newGraphID = sqlite->runInsert(sqlStatement);
                JasmineGraphServer *jasmineServer = new JasmineGraphServer();
                MetisPartitioner *partitioner = new MetisPartitioner(&sessionargs->sqlite);

                partitioner->loadDataSet(path, newGraphID);
                int result = partitioner->constructMetisFormat(Conts::GRAPH_TYPE_NORMAL);
                if (result == 0) {
                    string reformattedFilePath = partitioner->reformatDataSet(path, newGraphID);
                    partitioner->loadDataSet(reformattedFilePath, newGraphID);
                    partitioner->constructMetisFormat(Conts::GRAPH_TYPE_NORMAL_REFORMATTED);
                    partitioner->partitioneWithGPMetis();
                } else {
                    partitioner->partitioneWithGPMetis();
                }
                jasmineServer->uploadGraphLocally(newGraphID, Conts::GRAPH_TYPE_NORMAL);
                utils.deleteDirectory(utils.getHomeDir() + "/.jasminegraph/tmp/" + to_string(newGraphID));
                utils.deleteDirectory("/tmp/" + std::to_string(newGraphID));
            } else {
                frontend_logger.log("Graph data file does not exist on the specified path", "error");
                break;
            }
        } else if (line.compare(ADGR_CUST) == 0) {
            string message = "Select a custom graph upload option\n";
            write(connFd, message.c_str(), message.size());
            write(connFd, Conts::GRAPH_WITH::TEXT_ATTRIBUTES.c_str(), Conts::GRAPH_WITH::TEXT_ATTRIBUTES.size());
            write(connFd, "\n", 2);
            write(connFd, Conts::GRAPH_WITH::JSON_ATTRIBUTES.c_str(), Conts::GRAPH_WITH::TEXT_ATTRIBUTES.size());
            write(connFd, "\n", 2);
            write(connFd, Conts::GRAPH_WITH::XML_ATTRIBUTES.c_str(), Conts::GRAPH_WITH::TEXT_ATTRIBUTES.size());
            write(connFd, "\n", 2);

            //TODO :: Handle user inputting a value out of range
            char type[20];
            bzero(type, 21);
            read(connFd, type, 20);
            string graphType(type);

            // We get the name and the path to graph edge list and attribute list as a triplet separated by | .
            // (<name>|<path to edge list>|<path to attribute file>)
            message = "Send <name>|<path to edge list>|<path to attribute file>\n";
            write(connFd, message.c_str(), message.size());
            char graph_data[300];
            bzero(graph_data, 301);
            string name = "";
            string edgeListPath = "";
            string attributeListPath = "";

            read(connFd, graph_data, 300);

            std::time_t time = chrono::system_clock::to_time_t(chrono::system_clock::now());
            string uploadStartTime = ctime(&time);
            string gData(graph_data);

            Utils utils;
            gData = utils.trim_copy(gData, " \f\n\r\t\v");
            frontend_logger.log("Data received: " + gData, "info");

            std::vector<std::string> strArr = Utils::split(gData, '|');

            if (strArr.size() != 3) {
                frontend_logger.log("Message format not recognized", "error");
                break;
            }

            name = strArr[0];
            edgeListPath = strArr[1];
            attributeListPath = strArr[2];

            if (JasmineGraphFrontEnd::graphExists(edgeListPath, dummyPt)) {
                frontend_logger.log("Graph exists", "error");
                break;
            }

            if (utils.fileExists(edgeListPath) && utils.fileExists(attributeListPath)) {
                std::cout << "Paths exists" << endl;

                SQLiteDBInterface *sqlite = &sessionargs->sqlite;
                string sqlStatement =
                        "INSERT INTO graph (name,upload_path,upload_start_time,upload_end_time,graph_status_idgraph_status,"
                        "vertexcount,centralpartitioncount,edgecount) VALUES(\"" + name + "\", \"" + edgeListPath +
                        "\", \"" + uploadStartTime + "\", \"\",\"" + to_string(Conts::GRAPH_STATUS::LOADING) +
                        "\", \"\", \"\", \"\")";
                int newGraphID = sqlite->runInsert(sqlStatement);
                JasmineGraphServer *jasmineServer = new JasmineGraphServer();
                MetisPartitioner *partitioner = new MetisPartitioner(&sessionargs->sqlite);
                partitioner->loadContentData(attributeListPath, "1");
                partitioner->loadDataSet(edgeListPath, newGraphID);
                int result = partitioner->constructMetisFormat(Conts::GRAPH_TYPE_NORMAL);
                if (result == 0) {
                    string reformattedFilePath = partitioner->reformatDataSet(edgeListPath, newGraphID);
                    partitioner->loadDataSet(reformattedFilePath, newGraphID);
                    partitioner->constructMetisFormat(Conts::GRAPH_TYPE_NORMAL_REFORMATTED);
                    partitioner->partitioneWithGPMetis();
                } else {
                    partitioner->partitioneWithGPMetis();
                }
                //Graph type should be changed to identify graphs with attributes
                //because this graph type has additional attribute files to be uploaded
                jasmineServer->uploadGraphLocally(newGraphID, Conts::GRAPH_WITH_ATTRIBUTES);
                utils.deleteDirectory(utils.getHomeDir() + "/.jasminegraph/tmp/" + to_string(newGraphID));
                utils.deleteDirectory("/tmp/" + std::to_string(newGraphID));
            } else {
                frontend_logger.log("Graph data file does not exist on the specified path", "error");
                break;
            }
        } else if (line.compare(ADD_STREAM_KAFKA) == 0) {
            std::cout << STREAM_TOPIC_NAME << endl;
            write(connFd, STREAM_TOPIC_NAME.c_str(), STREAM_TOPIC_NAME.length());
            write(connFd, "\r\n", 2);

            // We get the name and the path to graph as a pair separated by |.
            char topic_name[300];
            bzero(topic_name, 301);

            read(connFd, topic_name, 300);

            Utils utils;
            string topic_name_s(topic_name);
            topic_name_s = utils.trim_copy(topic_name_s, " \f\n\r\t\v");
            std::cout << "data received : " << topic_name << endl;
            // After getting the topic name , need to close the connection and ask the user to send the data to given topic

            cppkafka::Configuration configs = {{"metadata.broker.list", "127.0.0.1:9092"},
                                               {"group.id",             "knnect"}};
            KafkaConnector kstream(configs);

            kstream.Subscribe(topic_name_s);
            while (true) {
                cout << "Waiting to receive message. . ." << endl;
                cppkafka::Message msg = kstream.consumer.poll();
                if (!msg) {
                    continue;
                }

                if (msg.get_error()) {
                    if (msg.is_eof()) {
                        cout << "Message end of file received!" << endl;
                    }
                    continue;
                }

                cout << "Received message on partition " << msg.get_topic() << "/" << msg.get_partition() << ", offset "
                     << msg.get_offset() << endl;
                cout << "Payload = " << msg.get_payload() << endl;
            }
        } else if (line.compare(RMGR) == 0){
            write(connFd, SEND.c_str(), FRONTEND_COMMAND_LENGTH);
            write(connFd, "\r\n", 2);

            // We get the name and the path to graph as a pair separated by |.
            char graph_id[300];
            bzero(graph_id, 301);
            string name = "";
            string path = "";

            read(connFd, graph_id, 300);

            string graphID(graph_id);

            Utils utils;
            graphID = utils.trim_copy(graphID, " \f\n\r\t\v");
            frontend_logger.log("Graph ID received: " + graphID, "info");

            if (JasmineGraphFrontEnd::graphExistsByID(graphID, dummyPt)) {
                frontend_logger.log("Graph with ID " + graphID + " is being deleted now", "info");
                JasmineGraphFrontEnd::removeGraph(graphID, dummyPt);
            } else {
                frontend_logger.log("Graph does not exist or cannot be deleted with the current hosts setting",
                                    "error");
            }
        } else if (line.compare(TRIANGLES) == 0) {
            // add RDF graph
            write(connFd, GRAPHID_SEND.c_str(), FRONTEND_COMMAND_LENGTH);
            write(connFd, "\r\n", 2);

            // We get the name and the path to graph as a pair separated by |.
            char graph_id_data[300];
            bzero(graph_id_data, 301);
            string name = "";

            read(connFd, graph_id_data, 300);

            string graph_id(graph_id_data);

            if (!JasmineGraphFrontEnd::graphExistsByID(graph_id,dummyPt)) {
                string error_message = "The specified graph id does not exist";
                write(connFd, error_message.c_str(), FRONTEND_COMMAND_LENGTH);
                write(connFd, "\r\n", 2);
            } else {
                auto begin = chrono::high_resolution_clock::now();
                vector<string> hostsList = utils.getHostList();
                int hostListLength = hostsList.size();
                long triangleCount = JasmineGraphFrontEnd::countTriangles(graph_id,dummyPt);
                auto end = chrono::high_resolution_clock::now();
                auto dur = end - begin;
                auto msDuration = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
            }
        } else {
            frontend_logger.log("Message format not recognized " + line, "error");
        }
    }
    frontend_logger.log("Closing thread " + to_string(pthread_self()) + " and connection", "info");
    close(connFd);
}

JasmineGraphFrontEnd::JasmineGraphFrontEnd(SQLiteDBInterface db) {
    this->sqlite = db;
}

int JasmineGraphFrontEnd::run() {
    int pId;
    int portNo = Conts::JASMINEGRAPH_FRONTEND_PORT;;
    int listenFd;
    socklen_t len;
    bool loop = false;
    struct sockaddr_in svrAdd;
    struct sockaddr_in clntAdd;

    //create socket
    listenFd = socket(AF_INET, SOCK_STREAM, 0);

    if (listenFd < 0) {
        frontend_logger.log("Cannot open socket", "error");
        return 0;
    }

    bzero((char *) &svrAdd, sizeof(svrAdd));

    svrAdd.sin_family = AF_INET;
    svrAdd.sin_addr.s_addr = INADDR_ANY;
    svrAdd.sin_port = htons(portNo);

    int yes = 1;

    if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
        perror("setsockopt");
        exit(1);
    }


    //bind socket
    if (bind(listenFd, (struct sockaddr *) &svrAdd, sizeof(svrAdd)) < 0) {
        frontend_logger.log("Cannot bind", "error");
        return 0;
    }

    listen(listenFd, 10);

    pthread_t threadA[20];
    len = sizeof(clntAdd);

    int noThread = 0;

    while (noThread < 20) {
        frontend_logger.log("Frontend Listening", "info");

        //this is where client connects. svr will hang in this mode until client conn
        connFd = accept(listenFd, (struct sockaddr *) &clntAdd, &len);

        if (connFd < 0) {
            frontend_logger.log("Cannot accept connection", "error");
            return 0;
        } else {
            frontend_logger.log("Connection successful", "info");
        }

        struct frontendservicesessionargs frontendservicesessionargs1;
        frontendservicesessionargs1.sqlite = this->sqlite;
        frontendservicesessionargs1.connFd = connFd;


        pthread_create(&threadA[noThread], NULL, frontendservicesesion,
                       &frontendservicesessionargs1);

        noThread++;
    }

    for (int i = 0; i < noThread; i++) {
        pthread_join(threadA[i], NULL);
    }


}

/**
 * This method checks if a graph exists in JasmineGraph.
 * This method uses the unique path of the graph.
 * @param basic_string
 * @param dummyPt
 * @return
 */
bool JasmineGraphFrontEnd::graphExists(string path, void *dummyPt) {
    bool result = true;
    string stmt =
            "SELECT COUNT( * ) FROM graph WHERE upload_path LIKE '" + path + "' AND graph_status_idgraph_status = '" +
            to_string(Conts::GRAPH_STATUS::OPERATIONAL) + "';";
    SQLiteDBInterface *sqlite = (SQLiteDBInterface *) dummyPt;
    std::vector<vector<pair<string, string>>> v = sqlite->runSelect(stmt);
    int count = std::stoi(v[0][0].second);
    if (count == 0) {
        result = false;
    }
    return result;
}

/**
 * This method checks if an accessible graph exists in JasmineGraph with the same unique ID.
 * @param id
 * @param dummyPt
 * @return
 */
bool JasmineGraphFrontEnd::graphExistsByID(string id, void *dummyPt) {
    bool result = true;
    string stmt = "SELECT COUNT( * ) FROM graph WHERE idgraph = " + id + " and graph_status_idgraph_status = " +
                  to_string(Conts::GRAPH_STATUS::OPERATIONAL);
    SQLiteDBInterface *sqlite = (SQLiteDBInterface *) dummyPt;
    std::vector<vector<pair<string, string>>> v = sqlite->runSelect(stmt);
    int count = std::stoi(v[0][0].second);
    if (count == 0) {
        result = false;
    }
    return result;
}

/**
 * This method removes a graph from JasmineGraph
 */
void JasmineGraphFrontEnd::removeGraph(std::string graphID, void *dummyPt) {
    vector<pair<string, string>> hostHasPartition;
    SQLiteDBInterface *sqlite = (SQLiteDBInterface *) dummyPt;
    vector<vector<pair<string, string>>> hostPartitionResults = sqlite->runSelect(
            "SELECT name, partition_idpartition FROM host_has_partition INNER JOIN host ON host_idhost = idhost WHERE "
            "partition_graph_idgraph = '" + graphID + "'");
    for (vector<vector<pair<string, string>>>::iterator i = hostPartitionResults.begin();
         i != hostPartitionResults.end(); ++i) {
        int count = 0;
        string hostname;
        string partitionID;
        for (std::vector<pair<string, string>>::iterator j = (i->begin()); j != i->end(); ++j) {
            if (count == 0) {
                hostname = j->second;
            } else {
                partitionID = j->second;
                hostHasPartition.push_back(pair<string, string>(hostname, partitionID));
            }
            count++;
        }
    }
    for (std::vector<pair<string, string>>::iterator j = (hostHasPartition.begin()); j != hostHasPartition.end(); ++j) {
        cout << "HOST ID : " << j->first << " Partition ID : " << j->second << endl;
    }
    sqlite->runUpdate("UPDATE graph SET graph_status_idgraph_status = " + to_string(Conts::GRAPH_STATUS::DELETING) +
                      " WHERE idgraph = " + graphID);

    JasmineGraphServer *jasmineServer = new JasmineGraphServer();
    jasmineServer->removeGraph(hostHasPartition, graphID);

    sqlite->runUpdate("DELETE FROM host_has_partition WHERE partition_graph_idgraph = " + graphID);
    sqlite->runUpdate("DELETE FROM partition WHERE graph_idgraph = " + graphID);
    sqlite->runUpdate("DELETE FROM graph WHERE idgraph = " + graphID);
}


long JasmineGraphFrontEnd::countTriangles(std::string graphId, void *dummyPt) {
    long result= 0;
    Utils utils;
    vector<std::string> hostList = utils.getHostList();
    int hostListSize = hostList.size();
    int counter = 0;
    std::vector<std::future<long>> intermRes;
    PlacesToNodeMapper placesToNodeMapper;

    string sqlStatement = "SELECT NAME,PARTITION_IDPARTITION FROM ACACIA_META.HOST_HAS_PARTITION INNER JOIN ACACIA_META.HOST ON HOST_IDHOST=IDHOST WHERE PARTITION_GRAPH_IDGRAPH=" + graphId + ";";

    SQLiteDBInterface *sqlite = (SQLiteDBInterface *) dummyPt;
    std::vector<vector<pair<string, string>>> results = sqlite->runSelect(sqlStatement);

    std::map<string, std::vector<string>> map;

    for (std::vector<vector<pair<string, string>>>::iterator i = results.begin(); i != results.end(); ++i) {
        std::vector<pair<string, string>> rowData = *i;

        string name = rowData.at(0).second;
        string partitionId = rowData.at(1).second;

        std::vector<string> partitionList = map[name];

        partitionList.push_back(partitionId);

        map[name] = partitionList;
    }

    for (int i=0;i<hostListSize;i++) {
        int k = counter;
        string host = placesToNodeMapper.getHost(i);
        std::vector<int> instancePorts = placesToNodeMapper.getInstancePort(i);
        string partitionId;

        std::vector<string> partitionList = map[host];

        if (partitionList.size() > 0) {
            auto iterator = partitionList.begin();
            partitionId = *iterator;
            partitionList.erase(partitionList.begin());
        }

        std::vector<int>::iterator portsIterator;

        for (portsIterator = instancePorts.begin(); portsIterator != instancePorts.end(); ++portsIterator) {
            int port = *portsIterator;
            intermRes.push_back(std::async(std::launch::async,JasmineGraphFrontEnd::getTriangleCount,atoi(graphId.c_str()),host,port,atoi(partitionId.c_str())));
        }

    }

    for (auto&& futureCall:intermRes) {
        result += futureCall.get();
    }

    long globalTriangleCount = JasmineGraphFrontEnd::countGlobalTriangles(atoi(graphId.c_str()), dummyPt);
    result += globalTriangleCount;
    return result;
}


long JasmineGraphFrontEnd::getTriangleCount(int graphId, std::string host, int port, int partitionId) {

    int sockfd;
    char data[300];
    bool loop = false;
    socklen_t len;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    Utils utils;
    long triangleCount;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        std::cerr << "Cannot accept connection" << std::endl;
        return 0;
    }

    if (host.find('@') != std::string::npos) {
        host = utils.split(host, '@')[1];
    }

    server = gethostbyname(host.c_str());
    if (server == NULL) {
        std::cerr << "ERROR, no host named " << server << std::endl;
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *) server->h_addr,
          (char *) &serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(port);
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "ERROR connecting" << std::endl;
        //TODO::exit
    }

    bzero(data, 301);
    write(sockfd, JasmineGraphInstanceProtocol::HANDSHAKE.c_str(), JasmineGraphInstanceProtocol::HANDSHAKE.size());
    frontend_logger.log("Sent : " + JasmineGraphInstanceProtocol::HANDSHAKE, "info");
    bzero(data, 301);
    read(sockfd, data, 300);
    string response = (data);

    response = utils.trim_copy(response, " \f\n\r\t\v");

    if (response.compare(JasmineGraphInstanceProtocol::HANDSHAKE_OK) == 0) {
        frontend_logger.log("Received : " + JasmineGraphInstanceProtocol::HANDSHAKE_OK, "info");
        string server_host = utils.getJasmineGraphProperty("org.jasminegraph.server.host");
        write(sockfd, server_host.c_str(), server_host.size());
        frontend_logger.log("Sent : " + server_host, "info");

        write(sockfd, JasmineGraphInstanceProtocol::TRIANGLES.c_str(),
              JasmineGraphInstanceProtocol::TRIANGLES.size());
        frontend_logger.log("Sent : " + JasmineGraphInstanceProtocol::TRIANGLES, "info");
        bzero(data, 301);
        read(sockfd, data, 300);
        response = (data);
        response = utils.trim_copy(response, " \f\n\r\t\v");

        if (response.compare(JasmineGraphInstanceProtocol::OK) == 0) {
            frontend_logger.log("Received : " + JasmineGraphInstanceProtocol::OK, "info");
            write(sockfd, std::to_string(graphId).c_str(), std::to_string(graphId).size());
            frontend_logger.log("Sent : Graph ID " + std::to_string(graphId), "info");

            bzero(data, 301);
            read(sockfd, data, 300);
            response = (data);
            response = utils.trim_copy(response, " \f\n\r\t\v");
        }

        if (response.compare(JasmineGraphInstanceProtocol::OK) == 0) {
            frontend_logger.log("Received : " + JasmineGraphInstanceProtocol::OK, "info");
            write(sockfd, std::to_string(partitionId).c_str(), std::to_string(partitionId).size());
            frontend_logger.log("Sent : Partition ID " + std::to_string(partitionId), "info");

            bzero(data, 301);
            read(sockfd, data, 300);
            response = (data);
            response = utils.trim_copy(response, " \f\n\r\t\v");
            triangleCount = atol(response.c_str());
            return triangleCount;
        }
    } else {
        frontend_logger.log("There was an error in the upload process and the response is :: " + response, "error");
    }

}

long JasmineGraphFrontEnd::countGlobalTriangles(int graphId, void *dummyPt) {
    long result;
    long startVid;
    long endVid;
    int centralPartitionCount = 0;
    std::string query = "select CENTRALPARTITIONCOUNT from ACACIA_META.GRAPH where IDGRAPH=" + std::to_string(graphId);

    SQLiteDBInterface *sqlite = (SQLiteDBInterface *) dummyPt;
    std::vector<vector<pair<string, string>>> results = sqlite->runSelect(query);

    map<long, unordered_set<long>> localSubgraphMap;

    if (results.size() > 0) {
        vector<pair<string, string>> resultPairVector = results.at(0);
        if (resultPairVector.size() >0) {
            std::string resultString = resultPairVector.at(0).second;
            centralPartitionCount = atoi(resultString.c_str());
        }
    }

    for (int i=0; i< centralPartitionCount; i++) {
        JasmineGraphHashMapCentralStore *centralStore = new JasmineGraphHashMapCentralStore(graphId, i);
        centralStore->loadGraph();

        map<long, unordered_set<long>> edgeList2 = centralStore->getUnderlyingHashMap();
        map<long, unordered_set<long>>::iterator edgeListIterator;
        long firstVertex = 0;

        for (edgeListIterator = edgeList2.begin(); edgeListIterator != edgeList2.end(); ++edgeListIterator) {
            firstVertex = edgeListIterator->first;
            unordered_set<long> hs = localSubgraphMap[firstVertex];

            unordered_set<long> hs2 = edgeListIterator->second;

            for (unordered_set<long>::iterator it = hs2.begin(); it != hs2.end(); ++it) {
                hs.insert(*it);
            }

            localSubgraphMap[firstVertex] = hs;
        }

    }

    map<long, unordered_set<long>> degreeMap;
    map<long,long> degreeReverseLookupMap;

    long degree =0;
    startVid =0;
    map<long,unordered_set<long>>::iterator localSubgraphMapIterator;

    for (localSubgraphMapIterator = localSubgraphMap.begin(); localSubgraphMapIterator != localSubgraphMap.end(); ++ localSubgraphMapIterator) {
        startVid = localSubgraphMapIterator->first;
        unordered_set<long> items = localSubgraphMapIterator->second;

        degree = items.size();

        unordered_set<long> degreeSet = degreeMap[degree];
        degreeSet.insert(startVid);
        degreeMap[degree] = degreeSet;
    }

    long triangleCount =0;
    long v1 = 0;
    long v2 = 0;
    long v3 = 0;
    map<long, map<long, std::vector<long>>> triangleTree;
    vector<long> degreeListVisited;

    map<long, unordered_set<long>>::iterator degreeMapIterator;

    for (degreeMapIterator = degreeMap.begin(); degreeMapIterator != degreeMap.end(); ++ degreeMapIterator) {
        long kkkk = degreeMapIterator->first;

        unordered_set<long> vVertices = degreeMap[kkkk];

        unordered_set<long>::iterator vVerticesIterator;

        for (vVerticesIterator = vVertices.begin(); vVerticesIterator != vVertices.end(); ++vVerticesIterator) {
            long v = *vVerticesIterator;
            unordered_set<long> uList = localSubgraphMap[v];
            unordered_set<long>::iterator uListIterator;
            for (uListIterator = uList.begin(); uListIterator !=uList.end(); ++uListIterator) {
                long u = *uListIterator;
                unordered_set<long> nuList = localSubgraphMap[u];
                unordered_set<long>::iterator itr4;
                for (itr4 = nuList.begin(); itr4!= nuList.end(); ++itr4) {
                    long nu = *itr4;
                    unordered_set<long> nwList = localSubgraphMap[nu];

                    unordered_set<long> :: iterator nwListSearchIterator = nwList.find(v);

                    if (nwListSearchIterator != nwList.end()) {
                        vector<long> tempVector;
                        tempVector.push_back(v);
                        tempVector.push_back(u);
                        tempVector.push_back(nu);

                        std::sort(tempVector.begin(),tempVector.end());

                        v1 = tempVector[0];
                        v2 = tempVector[1];
                        v3 = tempVector[2];

                        map<long, std::vector<long>> itemRes = triangleTree[v1];

                        std::vector<long> lst = itemRes[v2];

                        if (std::find(lst.begin(), lst.end(), v3) == lst.end()) {
                            lst.push_back(v3);
                            itemRes[v2] = lst;
                            triangleTree[v1] = itemRes;
                            triangleCount++;
                        }

                    }
                }
            }
        }
        degreeListVisited.push_back(kkkk);
        degreeMap.erase(kkkk);
    }
    result = triangleCount;

    return result;
}
