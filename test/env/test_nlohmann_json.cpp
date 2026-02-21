#include <iostream>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

int main()
{
    // Using (raw) string literals and json::parse
    json ex1 = json::parse(R"(
    {
        "pi": 3.141,
        "happy": true
    }
    )");
    std::cout << "json: " << ex1 << '\n';

    std::cout << "main done\n";
    return 0;
}