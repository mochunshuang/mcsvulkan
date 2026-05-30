#include <string>
#include <meta>
#include <iostream>
#include <algorithm>
#include <spanstream>

// The Library
namespace clap
{
    struct Flags
    {
        bool use_short;
        bool use_long;
    };

    template <typename T, Flags flags>
    struct Option
    {
        std::optional<T> initializer;

        Option() = default;
        Option(T t) : initializer(t) {}

        static constexpr bool use_short = flags.use_short;
        static constexpr bool use_long = flags.use_long;
    };

    consteval auto spec_to_opts(std::meta::info opts, std::meta::info spec)
        -> std::meta::info
    {
        std::vector<std::meta::nsdm_description> new_members;
        for (auto member : nonstatic_data_members_of(spec))
        {
            auto new_type = template_arguments_of(type_of(member))[0];
            new_members.push_back(
                make_nsdm_description(new_type, {.name = identifier_of(member)}));
        }
        return define_class(opts, new_members);
    }

    struct Clap
    {
        template <typename Spec>
        auto parse(this Spec const &spec, int argc, char **argv)
        {
            std::vector<std::string_view> cmdline(argv + 1, argv + argc);

            // check if cmdline contains --help, etc.

            struct Opts;
            static_assert(is_type(spec_to_opts(^^Opts, ^^Spec)));
            Opts opts;

            struct Z
            {
                std::meta::info spec;
                std::meta::info opt;
            };

            [:std::meta::expand([]() consteval {
                auto spec_members = nonstatic_data_members_of(^^Spec);
                auto opts_members = nonstatic_data_members_of(^^Opts);

                std::vector<Z> v;
                for (size_t i = 0; i != spec_members.size(); ++i)
                {
                    v.push_back({.spec = spec_members[i], .opt = opts_members[i]});
                }
                return v;
            }()):] >> [&]<auto Z> {
                constexpr auto sm = Z.spec;
                constexpr auto om = Z.opt;

                auto &cur = spec.[:sm:];

                // find the argument associated with this option
                auto it = std::find_if(
                    cmdline.begin(), cmdline.end(), [&](std::string_view arg) {
                        return cur.use_short && arg.size() == 2 && arg[0] == '-' &&
                                   arg[1] == identifier_of(sm)[0] ||
                               cur.use_long && arg.starts_with("--") &&
                                   arg.substr(2) == identifier_of(sm);
                    });

                if (it == cmdline.end())
                {
                    // no such argument
                    if constexpr (has_template_arguments(type_of(om)) &&
                                  template_of(type_of(om)) == ^^std::optional)
                    {
                        // the type is optional, sot he argument is too
                        return;
                    }
                    else if (cur.initializer)
                    {
                        // the type isn't optional, but an initializer is provided, use that
                        opts.[:om:] = *cur.initializer;
                        return;
                    }
                    else
                    {
                        std::cerr << "Missing required option " << identifier_of(sm)
                                  << '\n';
                        std::exit(EXIT_FAILURE);
                    }
                }
                else if (it + 1 == cmdline.end())
                {
                    std::cout << "Option " << *it << " for " << identifier_of(sm)
                              << " is missing a value\n";
                    std::exit(EXIT_FAILURE);
                }

                // alright, found our argument, try to parse it
                auto iss = std::ispanstream(it[1]);
                if (iss >> opts.[:om:]; !iss)
                {
                    std::cerr << "Failed to parse " << it[1] << " into option "
                              << identifier_of(sm) << " of type " << name_of(type_of(om))
                              << '\n';
                    std::exit(EXIT_FAILURE);
                }
            };

            return opts;
        }
    };
} // namespace clap

// The User
using namespace clap;
struct Args : Clap
{
    Option<std::string, Flags{.use_short = true, .use_long = true}> name;
    Option<int, Flags{.use_short = true, .use_long = true}> count = 1;
};

int main(int argc, char **argv)
{
    auto opts = Args{}.parse(argc, argv);

    for (int i = 0; i < opts.count; ++i)
    {                                                // opts.count has type int
        std::cout << "Hello " << opts.name << "!\n"; // opts.name has type std::string
    }
}