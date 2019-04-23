/**
Copyright 2019 JasmineGraph Team
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

#include <flatbuffers/flatbuffers.h>
#include "MetisPartitioner.h"
#include "../../util/Conts.h"


thread_local std::vector<string> partitionFileList;
thread_local std::vector<string> centralStoreFileList;
thread_local std::vector<string> partitionAttributeFileList;
thread_local std::vector<string> centralStoreAttributeFileList;

MetisPartitioner::MetisPartitioner(SQLiteDBInterface *sqlite) {
    this->sqlite = *sqlite;
}

void MetisPartitioner::loadDataSet(string inputFilePath, int graphID) {
    this->graphID = graphID;
    // Output directory is created under the users home directory '~/.jasminegraph/tmp/'
    this->outputFilePath = utils.getHomeDir() + "/.jasminegraph/tmp/" + std::to_string(this->graphID);

    // Have to call createDirectory twice since it does not support recursive directory creation. Could use boost::filesystem for path creation
    this->utils.createDirectory(utils.getHomeDir() + "/.jasminegraph/");
    this->utils.createDirectory(utils.getHomeDir() + "/.jasminegraph/tmp");
    this->utils.createDirectory(this->outputFilePath);

    this->utils.createDirectory("/tmp/" + std::to_string(this->graphID));
    std::ifstream dbFile;
    dbFile.open(inputFilePath, std::ios::binary | std::ios::in);

    int firstVertex = -1;
    int secondVertex = -1;
    string line;
    char splitter;

    std::getline(dbFile, line);

    if (!line.empty()) {
        if (line.find(" ") != std::string::npos) {
            splitter = ' ';
        } else if (line.find('\t') != std::string::npos) {
            splitter = '\t';
        } else if (line.find(",") != std::string::npos) {
            splitter = ',';
        }
    }

    while (!line.empty()) {
        string vertexOne;
        string vertexTwo;

        std::istringstream stream(line);
        std::getline(stream, vertexOne, splitter);
        stream >> vertexTwo;

        firstVertex = std::stoi(vertexOne);
        secondVertex = std::stoi(vertexTwo);

        if (!zeroflag) {
            if (firstVertex == 0 || secondVertex == 0) {
                zeroflag = true;
                std::cout << "Graph have zero vertex." << std::endl;
            }
        }

        std::vector<int> firstEdgeSet = graphStorageMap[firstVertex];
        std::vector<int> secondEdgeSet = graphStorageMap[secondVertex];

        std::vector<int> vertexEdgeSet = graphEdgeMap[firstVertex];

        if (firstEdgeSet.empty()) {
            vertexCount++;
            edgeCount++;
            firstEdgeSet.push_back(secondVertex);
            vertexEdgeSet.push_back(secondVertex);

        } else {
            if (std::find(firstEdgeSet.begin(), firstEdgeSet.end(), secondVertex) == firstEdgeSet.end()) {
                firstEdgeSet.push_back(secondVertex);
                vertexEdgeSet.push_back(secondVertex);
                edgeCount++;
            }
        }

        if (secondEdgeSet.empty()) {
            vertexCount++;
            secondEdgeSet.push_back(firstVertex);

        } else {
            if (std::find(secondEdgeSet.begin(), secondEdgeSet.end(), firstVertex) == secondEdgeSet.end()) {
                secondEdgeSet.push_back(firstVertex);
            }
        }

        graphStorageMap[firstVertex] = firstEdgeSet;
        graphStorageMap[secondVertex] = secondEdgeSet;
        graphEdgeMap[firstVertex] = vertexEdgeSet;


        if (firstVertex > largestVertex) {
            largestVertex = firstVertex;
        }

        if (secondVertex > largestVertex) {
            largestVertex = secondVertex;
        }

        std::getline(dbFile, line);
        while (!line.empty() && line.find_first_not_of(splitter) == std::string::npos) {
            std::getline(dbFile, line);
        }
    }

}

void MetisPartitioner::constructMetisFormat(string graph_type) {
    graphType = graph_type;
    int adjacencyIndex = 0;
    std::ofstream outputFile;
    //outputFile.open("/tmp/grf");
    string outputFileName = "/tmp/" + std::to_string(this->graphID) + "/grf";
    outputFile.open(outputFileName);

    if (zeroflag) {
        outputFile << (++vertexCount) << ' ' << (edgeCount) << std::endl;
    } else {
        outputFile << (vertexCount) << ' ' << (edgeCount) << std::endl;
    }
    this->totalEdgeCount = edgeCount;
    this->totalVertexCount = vertexCount;

    xadj.push_back(adjacencyIndex);
    for (int vertexNum = 0; vertexNum <= largestVertex; vertexNum++) {
        std::vector<int> vertexSet = graphStorageMap[vertexNum];
        std::sort(vertexSet.begin(), vertexSet.end());

        for (std::vector<int>::const_iterator i = vertexSet.begin(); i != vertexSet.end(); ++i) {
            if (zeroflag) {
                outputFile << (*i + 1) << ' ';
            } else {
                outputFile << *i << ' ';
            }
        }

        outputFile << std::endl;
    }
}

void MetisPartitioner::partitioneWithGPMetis() {
    char buffer[128];
    std::string result = "";
    FILE *headerModify;
    //FILE *input = popen("gpmetis /tmp/grf 4 2>&1", "r");
    string metisCommand = "gpmetis /tmp/" + std::to_string(this->graphID) + "/grf 4 2>&1";
    FILE *input = popen(metisCommand.c_str(), "r");
    if (input) {
        // read the input
        while (!feof(input)) {
            if (fgets(buffer, 128, input) != NULL) {
                result.append(buffer);
            }
        }
        pclose(input);
        if (!result.empty() && result.find("Premature") != std::string::npos) {
            vertexCount -= 1;
            string newHeader = std::to_string(vertexCount) + ' ' + std::to_string(edgeCount);
            //string command = "sed -i \"1s/.*/" + newHeader +"/\" /tmp/grf";
            string command = "sed -i \"1s/.*/" + newHeader + "/\" /tmp/" + std::to_string(this->graphID) + "/grf";
            char *newHeaderChar = new char[command.length() + 1];
            strcpy(newHeaderChar, command.c_str());
            headerModify = popen(newHeaderChar, "r");
            partitioneWithGPMetis();
        } else if (!result.empty() && result.find("out of bounds") != std::string::npos) {
            vertexCount += 1;
            string newHeader = std::to_string(vertexCount) + ' ' + std::to_string(edgeCount);
            //string command = "sed -i \"1s/.*/" + newHeader +"/\" /tmp/grf";
            string command = "sed -i \"1s/.*/" + newHeader + "/\" /tmp/" + std::to_string(this->graphID) + "/grf";
            char *newHeaderChar = new char[command.length() + 1];
            strcpy(newHeaderChar, command.c_str());
            headerModify = popen(newHeaderChar, "r");
            partitioneWithGPMetis();
            //However, I only found
        } else if (!result.empty() && result.find("However, I only found") != std::string::npos) {
            string firstDelimiter = "I only found";
            string secondDelimite = "edges in the file";
            unsigned first = result.find(firstDelimiter);
            unsigned last = result.find(secondDelimite);
            string newEdgeSize = result.substr(first + firstDelimiter.length() + 1,
                                               last - (first + firstDelimiter.length()) - 2);
            string newHeader = std::to_string(vertexCount) + ' ' + newEdgeSize;
            //this->totalEdgeCount = atoi(newEdgeSize.c_str());
            //string command = "sed -i \"1s/.*/" + newHeader +"/\" /tmp/grf";
            string command = "sed -i \"1s/.*/" + newHeader + "/\" /tmp/" + std::to_string(this->graphID) + "/grf";
            char *newHeaderChar = new char[command.length() + 1];
            strcpy(newHeaderChar, command.c_str());
            headerModify = popen(newHeaderChar, "r");
            partitioneWithGPMetis();
            //However, I only found
        } else if (!result.empty() && result.find("Timing Information") != std::string::npos) {
            idx_t partIndex[vertexCount];
            std::string line;
            string fileName = "/tmp/" + std::to_string(this->graphID) + "/grf.part.4";
            std::ifstream infile(fileName);
            //std::ifstream infile("/tmp/grf.part.4");
            int counter = 0;
            while (std::getline(infile, line)) {
                std::istringstream iss(line);
                int a;
                if (!(iss >> a)) {
                    break;
                } else {
                    partIndex[counter] = a;
                    counter++;
                }
            }

            createPartitionFiles(partIndex);
            string sqlStatement =
                    "UPDATE graph SET vertexcount = '" + std::to_string(this->totalVertexCount) + "' ,centralpartitioncount = '" +
                    std::to_string(this->nParts) + "' ,edgecount = '"+std::to_string(this->totalEdgeCount)+"' WHERE idgraph = '" +
                    std::to_string(this->graphID) + "'";
            this->sqlite.runUpdate(sqlStatement);
        }


        perror("popen");
    } else {
        perror("popen");
        // handle error
    }
}

void MetisPartitioner::createPartitionFiles(idx_t *part) {

    edgeMap = GetConfig::getEdgeMap();
    articlesMap = GetConfig::getAttributesMap();

    for (int vertex = 0; vertex < vertexCount; vertex++) {
        idx_t vertexPart = part[vertex];

        std::vector<int> partVertexSet = partVertexMap[vertexPart];

        partVertexSet.push_back(vertex);

        partVertexMap[vertexPart] = partVertexSet;
    }

    for (int vertex = 0; vertex < vertexCount; vertex++) {
        std::vector<int> vertexEdgeSet = graphEdgeMap[vertex];
        idx_t firstVertexPart = part[vertex];

        if (!vertexEdgeSet.empty()) {
            std::vector<int>::iterator it;
            for (it = vertexEdgeSet.begin(); it != vertexEdgeSet.end(); ++it) {
                int secondVertex = *it;
                int secondVertexPart = part[secondVertex];

                if (firstVertexPart == secondVertexPart) {
                    std::map<int, std::vector<int>> partEdgesSet = partitionedLocalGraphStorageMap[firstVertexPart];
                    std::vector<int> edgeSet = partEdgesSet[vertex];
                    edgeSet.push_back(secondVertex);


                    partEdgesSet[vertex] = edgeSet;
                    partitionedLocalGraphStorageMap[firstVertexPart] = partEdgesSet;
                } else {
                    std::map<int, std::vector<int>> partMasterEdgesSet = masterGraphStorageMap[firstVertexPart];
                    std::vector<int> edgeSet = partMasterEdgesSet[vertex];
                    edgeSet.push_back(secondVertex);
                    partMasterEdgesSet[vertex] = edgeSet;
                    masterGraphStorageMap[firstVertexPart] = partMasterEdgesSet;
                }
            }
        }
    }

    partitionFileList.clear();
    centralStoreFileList.clear();
    partitionAttributeFileList.clear();
    centralStoreAttributeFileList.clear();

    for (int part = 0; part < nParts; part++) {
        int partEdgeCount = 0;
        std::vector<int> partVertexList;
        string outputFilePart = outputFilePath + "/" + std::to_string(this->graphID) + "_" + std::to_string(part);
        string outputFilePartMaster =
                outputFilePath + "/" + std::to_string(this->graphID) + "_centralstore_" + std::to_string(part);
        string attributeFilePart =
                outputFilePath + "/" + std::to_string(this->graphID) + "_attributes_" + std::to_string(part);
        string attributeFilePartMaster =
                outputFilePath + "/" + std::to_string(this->graphID) + "_centralstore_attributes_" +
                std::to_string(part);

        std::map<int, std::vector<int>> partEdgeMap = partitionedLocalGraphStorageMap[part];
        std::map<int, std::vector<int>> partMasterEdgeMap = masterGraphStorageMap[part];


        if (graphType == Conts::GRAPH_TYPE_RDF) {
            std::map<long, std::vector<string>> partitionedEdgeAttributes;
            std::map<long, std::vector<string>> centralStoreEdgeAttributes;



            //edge attribute separation for partition files
            for (auto it = partEdgeMap.begin(); it != partEdgeMap.end(); ++it) {
                for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
                    auto entry = edgeMap.find(make_pair(it->first, *it2));
                    long article_id = entry->second;
                    std::vector<string> attributes;
                    auto array = (articlesMap.find(article_id))->second;

                    for (int itt = 0; itt < 7; itt++) {
                        string element = (array)[itt];
                        attributes.push_back(element);

                    }

                    partitionedEdgeAttributes.insert({article_id, attributes});


                }
            }




            //edge attribute separation for central store files
            for (auto it = partMasterEdgeMap.begin(); it != partMasterEdgeMap.end(); ++it) {
                for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
                    auto entry = edgeMap.find(make_pair(it->first, *it2));
                    long article_id = entry->second;
                    std::vector<string> attributes;
                    auto array = (articlesMap.find(article_id))->second;

                    for (int itt = 0; itt < 7; itt++) {
                        string element = (array)[itt];
                        attributes.push_back(element);
                    }


                    centralStoreEdgeAttributes.insert({article_id, attributes});


                }
            }

            JasmineGraphHashMapLocalStore *hashMapLocalStore = new JasmineGraphHashMapLocalStore();
            hashMapLocalStore->storeAttributes(partitionedEdgeAttributes, attributeFilePart);
            hashMapLocalStore->storeAttributes(centralStoreEdgeAttributes, attributeFilePartMaster);
        }


        if (!partEdgeMap.empty()) {
            std::ofstream localFile(outputFilePart);

            if (localFile.is_open()) {
                for (int vertex = 0; vertex < vertexCount; vertex++) {
                    std::vector<int> destinationSet = partEdgeMap[vertex];
                    if (!destinationSet.empty()) {
                        partVertexList.push_back(vertex);
                        for (std::vector<int>::iterator itr = destinationSet.begin();
                             itr != destinationSet.end(); ++itr) {

                            string edge;

                            if (graphType == Conts::GRAPH_TYPE_RDF) {
                                auto entry = edgeMap.find(make_pair(vertex, (*itr)));
                                long article_id = entry->second;

                                string edge = std::to_string(vertex) + " " + std::to_string((*itr)) + " " +
                                              std::to_string(article_id);
                            } else {
                                string edge = std::to_string(vertex) + " " + std::to_string((*itr));

                            }
                            localFile << edge;
                            localFile << "\n";
                            partEdgeCount++;
                            partVertexList.push_back(*itr);
                        }
                    }
                }
            }

            localFile.flush();
            localFile.close();

            std::ofstream masterFile(outputFilePartMaster);

            if (masterFile.is_open()) {
                for (int vertex = 0; vertex < vertexCount; vertex++) {
                    std::vector<int> destinationSet = partMasterEdgeMap[vertex];
                    if (!destinationSet.empty()) {
                        for (std::vector<int>::iterator itr = destinationSet.begin();
                             itr != destinationSet.end(); ++itr) {
                            string edge;

                            if (graphType == Conts::GRAPH_TYPE_RDF) {
                                auto entry = edgeMap.find(make_pair(vertex, (*itr)));
                                long article_id = entry->second;

                                string edge = std::to_string(vertex) + " " + std::to_string((*itr)) + " " +
                                              std::to_string(article_id);
                            } else {
                                string edge = std::to_string(vertex) + " " + std::to_string((*itr));

                            }

                            masterFile << edge;
                            masterFile << "\n";
                        }
                    }
                }
            }

            masterFile.flush();
            masterFile.close();

        }
        std::sort(partVertexList.begin(), partVertexList.end());
        int partVertexCount = std::unique(partVertexList.begin(), partVertexList.end()) - partVertexList.begin();
        string sqlStatement =
                "INSERT INTO partition (idpartition,graph_idgraph,vertexcount,edgecount) VALUES(\"" +
                std::to_string(part) + "\", \"" + std::to_string(this->graphID) +
                "\", \"" + std::to_string(partVertexCount) + "\",\"" + std::to_string(partEdgeCount) + "\")";
        this->sqlite.runUpdate(sqlStatement);

        //Compress part files
        this->utils.compressFile(outputFilePart);
        partitionFileList.push_back(outputFilePart + ".gz");
        this->utils.compressFile(outputFilePartMaster);
        centralStoreFileList.push_back(outputFilePartMaster + ".gz");
        if (graphType == Conts::GRAPH_TYPE_RDF){
            this->utils.compressFile(attributeFilePart);
            partitionAttributeFileList.push_back(attributeFilePart + ".gz");
            this->utils.compressFile(attributeFilePartMaster);
            centralStoreAttributeFileList.push_back(attributeFilePartMaster + ".gz");
        }
    }
}

std::vector<string> MetisPartitioner::getPartitionFiles() {
    return partitionFileList;
}

std::vector<string> MetisPartitioner::getCentalStoreFiles() {
    return centralStoreFileList;
}

std::vector<string> MetisPartitioner::getPartitionAttributeFiles() {
    return partitionAttributeFileList;
}

std::vector<string> MetisPartitioner::getCentralStoreAttributeFiles() {
    return centralStoreAttributeFileList;
}
