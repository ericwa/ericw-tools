/*  Copyright (C) 2016 Eric Wasylishen

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

 See file, 'COPYING', for details.
 */

#include "common/settings.hh"
#include "common/threads.hh"
#include "common/fs.hh"

namespace settings
{
setting_base::setting_base(
    setting_container *dictionary, const nameset &names, const setting_group *group, const char *description)
    : _names(names), _group(group), _description(description)
{
    Q_assert(_names.size() > 0);

    if (dictionary) {
        dictionary->registerSetting(this);
    }
}

setting_group performance_group{"Performance", 10};
setting_group logging_group{"Logging", 5};

[[noreturn]] void setting_container::printHelp()
{
    fmt::print("{}usage: {} [-help/-h/-?] [-options] {}\n\n", usage, programName, remainderName);

    for (auto grouped : grouped()) {
        if (grouped.first) {
            fmt::print("{}:\n", grouped.first->name);
        }

        for (auto setting : grouped.second) {
            size_t numPadding = max(static_cast<size_t>(0), 28 - (setting->primaryName().size() + 4));
            fmt::print("  -{} {:{}}    {}\n", setting->primaryName(), setting->format(), numPadding,
                setting->getDescription());

            for (int i = 1; i < setting->names().size(); i++) {
                fmt::print("   \\{}\n", setting->names()[i]);
            }
        }

        printf("\n");
    }

    exit(0);
}

void setting_container::printSummary()
{
    LogPrint("\n--- Options Summary ---\n");
    for (auto setting : _settings) {
        if (setting->isChanged()) {
            LogPrint("    \"{}\" was set to \"{}\" (from {})\n", setting->primaryName(), setting->stringValue(),
                setting->sourceString());
        }
    }
    LogPrint("\n");
}

std::vector<std::string> setting_container::parse(parser_base_t &parser)
{
    // the settings parser loop will continuously eat tokens as long as
    // it begins with a -; once we have no more settings to consume, we
    // break out of this loop and return the remainder.
    while (true) {
        // end of cmd line
        if (!parser.parse_token(PARSE_PEEK)) {
            break;
        }

        // end of options
        if (parser.token[0] != '-') {
            break;
        }

        // actually eat the token since we peeked above
        parser.parse_token();

        // remove leading hyphens. we support any number of them.
        while (parser.token.front() == '-') {
            parser.token.erase(parser.token.begin());
        }

        if (parser.token.empty()) {
            throw parse_exception("stray \"-\" in command line; please check your parameters");
        }

        if (parser.token == "help" || parser.token == "h" || parser.token == "?") {
            printHelp();
        }

        auto setting = findSetting(parser.token);

        if (!setting) {
            throw parse_exception(fmt::format("unknown option \"{}\"", parser.token));
        }

        // pass off to setting to parse; store
        // name for error message below
        std::string token = std::move(parser.token);

        if (!setting->parse(token, parser, true)) {
            throw parse_exception(
                fmt::format("invalid value for option \"{}\"; should be in format {}", token, setting->format()));
        }
    }

    // return remainder
    std::vector<std::string> remainder;

    while (true) {
        if (parser.at_end() || !parser.parse_token()) {
            break;
        }

        remainder.emplace_back(std::move(parser.token));
    }

    return remainder;
}

void common_settings::setParameters(int argc, const char **argv)
{
    programName = fs::path(argv[0]).stem().string();
}

void common_settings::postinitialize(int argc, const char **argv)
{
    printSummary();

    configureTBB(threads.value());

    if (verbose.value()) {
        log_mask |= 1 << LOG_VERBOSE;
    }

    if (nopercent.value()) {
        log_mask &= ~(1 << LOG_PERCENT);
    }

    if (quiet.value()) {
        log_mask &= ~((1 << LOG_PERCENT) | (1 << LOG_STAT) | (1 << LOG_PROGRESS));
    }
}
} // namespace settings
