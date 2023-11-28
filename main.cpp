#include <iostream>
#include <fstream>
#include <vector>
#include <exception>
#include <gtest/gtest.h>
#include <algorithm>
#include "nlohmann/json.hpp"
#include <sstream>
#include <thread>
#include <mutex>


std::string wordCleaning(std::string word) {
    while ((word.back() >= 32 and word.back() <= 47) or (word.back() >= 58 and word.back() <= 64)
           or (word.back() >= 91 and word.back() <= 96) or (word.back() >= 123 and word.back() <= 127)){
        word.erase(word.end()-1);
    }
    while ((word.front() >= 32 and word.front() <= 47) or (word.front() >= 58 and word.front() <= 64)
           or (word.front() >= 91 and word.front() <= 96) or (word.front() >= 123 and word.front() <= 127)){
        word.erase(0,1);
    }
    return word;
}

std::mutex freqAccess;

struct Entry {
    size_t docId, count;
};

struct RelativeIndex{
    size_t docId;
    float rank;
    bool operator == (const RelativeIndex& other) const {
        return (docId == other.docId && rank == other.rank);
    }
};

class OpeningError: public std::exception {
private:
    std::string message;
public:
    OpeningError(const std::string& fileName): message{ "Failed to open file: " + fileName + "."} {}
    const char* what() const noexcept override{
        return message.c_str();
    }
};

class JsonFileContainingError: public std::exception {
private:
    std::string message;
public:
    JsonFileContainingError(const std::string& fileName, const std::string& containerName): message{"Failed to find container "+containerName+" in file "+fileName+"."} {}
    const char* what() const noexcept override{
        return message.c_str();
    }
};



class ConverterJSON {
private:
    const std::string configJsonPath = "..\\config.json";
    nlohmann::json configJsonFile;
    const std::string requestsJsonPath = "..\\requests.json";
    nlohmann::json requestsJsonFile;
    const std::string answersJsonPath = "..\\answers.json";
    nlohmann::json answersJsonFile;


public:
    ConverterJSON(){
        std::ifstream ifSJsonFile(configJsonPath);
        if (ifSJsonFile.is_open()) {
            ifSJsonFile >> configJsonFile;
        } else {
            throw OpeningError(configJsonPath);
        }
        ifSJsonFile.close();
        ifSJsonFile.open(requestsJsonPath);
        if (ifSJsonFile.is_open()) {
            ifSJsonFile >> requestsJsonFile;
        } else {
            throw OpeningError(requestsJsonPath);
        }
        ifSJsonFile.close();
    };

    std::vector<std::string> GetTextDocuments(){
        std::vector<std::string> docums;
        if (configJsonFile.contains("files")) {
            for (auto i: configJsonFile["files"]) {
                std::string bufPath = i;
                std::replace(bufPath.begin(), bufPath.end(), '/', '\\');
                std::ifstream subFile(bufPath);
                if (subFile.is_open()) {
                    std::ostringstream sstr;
                    sstr << subFile.rdbuf();
                    docums.push_back(sstr.str());
                } else {
                    throw OpeningError(bufPath);
                }
            }
        } else {
            throw JsonFileContainingError(configJsonPath, "files");
        }

        return docums;
    };

    int GetResponsesLimit(){
        if (configJsonFile.contains("config")) {
            if (configJsonFile["config"].contains("max_responses")) {
                return (int) configJsonFile["config"]["max_responses"];
            } else {
                throw JsonFileContainingError(configJsonPath, "max_responses");
            }
        } else {
            throw JsonFileContainingError(configJsonPath, "config");
        }
    };

    std::vector<std::string> GetRequests(){
        std::vector <std::string> requests = {};
        if (requestsJsonFile.contains("requests")) {
            for (auto i: requestsJsonFile["requests"]) {
                requests.push_back((std::string)i);
            }
            return requests;
        } else {
            throw JsonFileContainingError(configJsonPath, "requests");
        }
    };

    void putAnswers(std::vector<std::vector<std::pair<int, float>>>answers){
        for (int i = 0; i < answers.size(); i++) {
            std::string strI = "";
            for (int n = 0; n < 3-std::to_string(i).length();n++) {
                strI += "0";
            }
            strI+=std::to_string(i+1);


            if (!answers[i].empty()) {
                answersJsonFile["answers"]["request" + strI]["result"] = "true";
                for (int j = 0; j < answers[i].size(); j++) {
                    nlohmann::json::value_type block;
                    block["docid"] = answers[i][j].first;
                    block["rank"] = answers[i][j].second;
                    answersJsonFile["answers"]["request" + strI]["relevance"].push_back(block);
                }
            }
            std::ofstream ofstreamJsonFile(answersJsonPath);
            ofstreamJsonFile << answersJsonFile;
            ofstreamJsonFile.close();
        }
    };


};

class InvertedIndex {
private:
    std::map<std::string, std::vector<Entry>> freqDictionary;
    std::vector<std::string> docs = {};

    void convertDocumentsIntoDatabase(std::string txt, size_t num) {
        std::map<std::string,size_t> words;
        std::istringstream iss(txt);
        std::string buf;
        while (!iss.eof()){
            iss >> buf;
            buf = wordCleaning(buf);
            if (words.count(buf) == 0){
                words.insert(std::pair<std::string,size_t>(buf,1));
            } else {
                words[buf] += 1;
            }
        }
        for (auto j = words.begin(); j != words.end(); j++) {
            freqAccess.lock();
            if (freqDictionary.count(j->first) == 0) {
                std::pair<std::string,std::vector<Entry>> PaBuf(j->first,{{num, j->second}});
                freqDictionary.insert(PaBuf);
            } else {
                freqDictionary[j->first].push_back({num,j->second});
            }
            freqAccess.unlock();
        }
    }

public:
    void UpdateDocumentBase(const std::vector<std::string> doc) {

        docs = doc;
        std::thread file_1(&InvertedIndex::convertDocumentsIntoDatabase, this, docs[0], 0);
        std::thread file_2(&InvertedIndex::convertDocumentsIntoDatabase, this, docs[1], 1);
        std::thread file_3(&InvertedIndex::convertDocumentsIntoDatabase, this, docs[2], 2);
        std::thread file_4(&InvertedIndex::convertDocumentsIntoDatabase, this, docs[3], 3);

        file_1.join();
        file_2.join();
        file_3.join();
        file_4.join();
    }

    auto getFreqDictionary() {
        return freqDictionary;
    }
};

class SearchServer {
private:
    InvertedIndex index;
    ConverterJSON converter;
    std::map<std::string, std::vector<Entry>> freqDictionary;
public:
    SearchServer(InvertedIndex& idx) : index(idx){
        freqDictionary = idx.getFreqDictionary();
    };

    std::vector<std::string> requestsInput() {
        return converter.GetRequests();
    };

    std::vector<std::vector<RelativeIndex>> search(const std::vector<std::string>& requestsInput){
        std::vector<std::vector<RelativeIndex>> result = {};
        for (int i = 0; i < requestsInput.size(); i++) {
            std::map<std::string, Entry> wordRelevance;
            std::vector<int> absoluteRelevance = {};
            std::vector<RelativeIndex> relevantRelevance = {};
            result.push_back({});
            std::istringstream iss(requestsInput[i]);
            std::string buf;
            std::vector<std::string> uniqueWords = {};
            while (!iss.eof()){
                iss >> buf;
                buf = wordCleaning(buf);
                if (std::count(uniqueWords.begin(), uniqueWords.end(), buf) == 0){
                    uniqueWords.push_back(buf);
                } else {

                };
            }
            for (auto j : uniqueWords) {
                for (size_t k = 0; k < converter.GetTextDocuments().size(); k++){
                    if (freqDictionary.count(j) != 0) {
                        wordRelevance.insert(std::pair<std::string, Entry>(j, (Entry) {k, freqDictionary[j][k].count}));
                    } else {
                        wordRelevance.insert(std::pair<std::string, Entry>(j, (Entry) {k, 0}));
                    }
                }
            }
            for (size_t j = 0; j < converter.GetTextDocuments().size(); j++) {
                int currentAbsRelevence = 0;
                for (auto k : wordRelevance) {
                    if (k.second.docId == j) {
                        currentAbsRelevence += k.second.count;
                    }
                }
                absoluteRelevance.push_back(currentAbsRelevence);
            }
            for (int j = 0; j < absoluteRelevance.size();j++) {
                relevantRelevance.push_back({(size_t)j,(float)absoluteRelevance[j]/ *std::max_element(absoluteRelevance.begin(),absoluteRelevance.end())});
            }
            result.push_back(relevantRelevance);
        }
        return result;
    };

};

int main(int argc, char** argv) {

    ConverterJSON conv;
    std::map<std::string, std::vector<Entry>> freqDictionary;
    std::vector<std::string> texts;
    std::vector<std::string> requests;
    InvertedIndex inv;
    inv.UpdateDocumentBase(conv.GetTextDocuments());
    try {
        SearchServer searchSer(inv);
        searchSer.search(searchSer.requestsInput());
        texts = conv.GetTextDocuments();
        requests = conv.GetRequests();
        conv.putAnswers({{{2,5.34},{8,12.7}},{{1,7.67},{2,34.67},{3,6.4}}});
    } catch (OpeningError ex) {
        std::cerr << ex.what() << std::endl;
        return 1;
    } catch (JsonFileContainingError ex) {
        std::cerr << ex.what() << std::endl;
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();

    return 0;
}
