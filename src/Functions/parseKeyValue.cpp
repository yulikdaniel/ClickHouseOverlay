#include "parseKeyValue.h"

#include <Columns/ColumnMap.h>
#include <Columns/ColumnsNumber.h>
#include <DataTypes/DataTypeMap.h>
#include <DataTypes/DataTypeString.h>
#include <Common/assert_cast.h>
#include <Functions/keyvaluepair/src/KeyValuePairExtractorBuilder.h>
#include <Functions/ReplaceStringImpl.h>

namespace DB
{

/*
 * In order to leverage DB::ReplaceStringImpl for a better performance, the default escaping processor needs
 * to be overriden by a no-op escaping processor. DB::ReplaceStringImpl does in-place replacing and leverages the
 * Volnitsky searcher.
 * */
struct NoOpEscapingProcessor : KeyValuePairEscapingProcessor<ParseKeyValue::EscapingProcessorOutput>
{
    explicit NoOpEscapingProcessor(char) {}

    Response process(const ResponseViews & response_views) const override
    {
        return response_views;
    }
};

ParseKeyValue::ParseKeyValue()
: return_type(std::make_shared<DataTypeMap>(std::make_shared<DataTypeString>(), std::make_shared<DataTypeString>()))
{
}

String ParseKeyValue::getName() const
{
    return name;
}

ColumnPtr ParseKeyValue::executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr &, size_t) const
{
    auto [data_column, escape_character, key_value_pair_delimiter, item_delimiter, enclosing_character, value_special_characters_allow_list] = parseArguments(arguments);

    auto extractor = getExtractor(escape_character, key_value_pair_delimiter, item_delimiter, enclosing_character, value_special_characters_allow_list);

    return parse(extractor, data_column);
}

bool ParseKeyValue::isVariadic() const
{
    return true;
}

bool ParseKeyValue::isSuitableForShortCircuitArgumentsExecution(const DataTypesWithConstInfo & /*arguments*/) const
{
    return false;
}

size_t ParseKeyValue::getNumberOfArguments() const
{
    return 0u;
}

DataTypePtr ParseKeyValue::getReturnTypeImpl(const DataTypes & /*arguments*/) const
{
    return return_type;
}

ParseKeyValue::ParsedArguments ParseKeyValue::parseArguments(const ColumnsWithTypeAndName & arguments) const
{
    if (arguments.empty()) {
        // throw exception
        return {};
    }

    auto data_column = arguments[0].column;

    if (arguments.size() == 1u)
    {
        return ParsedArguments {
            data_column,
            {},
            {},
            {},
            {},
            {}
        };
    }

    auto escape_character = arguments[1].column->getDataAt(0).toView().front();

    if (arguments.size() == 2u)
    {
        return ParsedArguments {
            data_column,
            escape_character,
            {},
            {},
            {},
            {}
        };
    }

    auto key_value_pair_delimiter = arguments[2].column->getDataAt(0).toView().front();

    if (arguments.size() == 3u)
    {
        return ParsedArguments {
            data_column,
            escape_character,
            key_value_pair_delimiter,
            {},
            {},
            {}
        };
    }

    auto item_delimiter = arguments[3].column->getDataAt(0).toView().front();

    if (arguments.size() == 4u)
    {
        return ParsedArguments {
            data_column,
            escape_character,
            key_value_pair_delimiter,
            item_delimiter,
            {},
            {}
        };
    }

    auto enclosing_character = arguments[4].column->getDataAt(0).toView().front();

    if (arguments.size() == 5u)
    {
        return ParsedArguments {
            data_column,
            escape_character,
            key_value_pair_delimiter,
            item_delimiter,
            enclosing_character,
            {
            }
        };
    }

    return ParsedArguments {
        data_column,
        escape_character,
        key_value_pair_delimiter,
        item_delimiter,
        enclosing_character,
        {
        }
    };
}

std::shared_ptr<KeyValuePairExtractor<ParseKeyValue::EscapingProcessorOutput>> ParseKeyValue::getExtractor(
    CharArgument escape_character, CharArgument key_value_pair_delimiter, CharArgument item_delimiter,
    CharArgument enclosing_character, SetArgument value_special_characters_allow_list) const
{
    auto builder = KeyValuePairExtractorBuilder<ParseKeyValue::EscapingProcessorOutput>();

    if (escape_character) {
        builder.withEscapeCharacter(escape_character.value());
    }

    if (key_value_pair_delimiter) {
        builder.withKeyValuePairDelimiter(key_value_pair_delimiter.value());
    }

    if (item_delimiter) {
        builder.withItemDelimiter(item_delimiter.value());
    }

    if (enclosing_character) {
        builder.withEnclosingCharacter(enclosing_character.value());
    }

    builder.withEscapingProcessor<NoOpEscapingProcessor>();

    builder.withValueSpecialCharacterAllowList(value_special_characters_allow_list);

    return builder.build();
}

ColumnPtr ParseKeyValue::parse(std::shared_ptr<KeyValuePairExtractor<ParseKeyValue::EscapingProcessorOutput>> extractor, ColumnPtr data_column) const
{
    auto offsets = ColumnUInt64::create();

    auto keys = ColumnString::create();
    auto values = ColumnString::create();

    auto row_offset = 0u;

    for (auto i = 0u; i < data_column->size(); i++)
    {
        auto row = data_column->getDataAt(i).toString();

        // TODO avoid copying
        auto response = extractor->extract(row);

        for (auto [key, value] : response)
        {
            keys->insert(key);
            values->insert(value);

            row_offset++;
        }

        offsets->insert(row_offset);
    }

    auto keys2 = ColumnString::create();
    auto values2 = ColumnString::create();

    ReplaceStringImpl<ReplaceStringTraits::Replace::All>::vector(keys->getChars(), keys->getOffsets(), "\\", "", keys2->getChars(), keys2->getOffsets());
    ReplaceStringImpl<ReplaceStringTraits::Replace::All>::vector(values->getChars(), values->getOffsets(), "\\", "", values2->getChars(), values2->getOffsets());

    ColumnPtr keys_ptr = std::move(keys2);

    return ColumnMap::create(keys_ptr, std::move(values2), std::move(offsets));
}

REGISTER_FUNCTION(ParseKeyValue)
{
    factory.registerFunction<ParseKeyValue>();
}

}
