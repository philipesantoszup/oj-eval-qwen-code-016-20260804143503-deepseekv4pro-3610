#include "bpt.h"
#include <iostream>
#include <sstream>
#include <string>

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    BPTree tree;
    if (!tree.is_open()) {
        return 1;
    }

    int n;
    std::cin >> n;
    std::cin.ignore();  // skip newline after n

    std::string line;
    for (int i = 0; i < n; i++) {
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "insert") {
            std::string index;
            int value;
            iss >> index >> value;
            tree.insert(index, value);
        } else if (cmd == "delete") {
            std::string index;
            int value;
            iss >> index >> value;
            tree.remove(index, value);
        } else if (cmd == "find") {
            std::string index;
            iss >> index;
            std::vector<int> results = tree.find(index);
            if (results.empty()) {
                std::cout << "null\n";
            } else {
                for (size_t j = 0; j < results.size(); j++) {
                    if (j > 0) std::cout << ' ';
                    std::cout << results[j];
                }
                std::cout << '\n';
            }
        }
    }

    return 0;
}