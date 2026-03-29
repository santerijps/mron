# MRON - Minimal (yet) Rich Object Notation

MRON is an object notation designed to be readable and lightweight. The aim is that MRON could be used as configuration files (.mron file extension) due to its readability, but also as a efficient data format for data transfer.

This repository defines the syntax of MRON and implements a program called "mronc" which can be used to translate .mron files to .json files and vice versa.

## Usage

mronc outputs JSON in an unformatted style. You can use e.g. jq for formatting the JSON.
mronc does, however, format MRON into a readable style.

```sh
# mronc infers the input file type from the file extension
mronc config.mron # outputs JSON to stdout
mronc config.json # outputs MRON to stdout

mronc config.mron -o config.json # outputs JSON to config.json
mronc config.json -o config.mron # outputs MRON to config.json
```

## Motivations

JSON is too verbose and noisy. It forces you to insert double quotes everywhere as well as colons. However, JSON can be represented on a single line due to its syntax. MRON can also be represented on a single line (although this won't be as readable) while using less "filler" characters.

CSV is as good as space-efficient data transfer gets. Each row represents columns of data, where commas separate each value from one another. MRON provides the possibility for a CSV-like syntax, ensuring space-efficiency.

YAML is very readable, but its syntax forces you to a specific format. Nested YAML cannot be written on one line, due to its rules of indendation. MRON is as easy (or easier) to read as YAML, while flexible enough to not force a specific formatting style.

## Rules

### Comments

Comments can be used to add documentation into .mron files.
Comments come in the following kinds

- Single-line comment
- Multiline comment

#### Single-line comment

1. Starts with the hash character #
2. Every character after the hash character is considered to be the content of the comment
3. The comment will be lost in translation

#### Multi-line comment

1. Starts with three hash characters ###
2. Ends with three hash characters ###
3. Everything else between the start and end are considered to be the content of the comment

### Whitespace

- All whitespace characters (spaces, tabs and newlines) are treated as the same character
- Whitespace characters usually denote the end of a key or value

### Keys

1. Allowed characters
    - Alphabetic
    - Digits
    - Underscores
    - Dashes
2. Key length is 1 or more characters
3. Key starts with an underscore or an alphabetic character

### Values

These are the available value types:

- String
- Number
- Boolean
- Record
- List

#### String

1. Starts with a double quote "
2. Ends with a double quote "
3. Quotes inside the string must be escaped with the backslash \ character
4. The start and end double quotes must be on the same line
5. Characters between the start and end double quotes is considered the actual value
6. Empty strings are allowed
    - E.g. ""

#### Number

1. Covers both integers and decimal numbers
2. Allowed characters
    - Digits 0 to 9
    - Underscores _
    - Commas ,
    - Periods .
3. Must start with a digit
4. Must end with a digit
5. Underscores and commas can be optionally used to make large numbers easier to read e.g. 1000000 can be represented as 1,000,000 or 1_000_000
6. Can have any number of underscores or commas
7. Period denotes a decimal number, with the integer part on the left and the decimal part on the right
8. One period per number is allowed

#### Boolean

1. Allowed character combinations
    - true
    - yes
    - false
    - no
2. true and yes are considered truthy
3. false and no are considered falsy

#### Record

Records are types than enclose data that can be accessed by first referencing the records name and the the keys inside the record.

1. Starts with a left brace {
2. Ends with a right brace }
3. Anything between the start and end must be
    - Empty, e.g. {}
    - Valid MRON key-value pairs

#### List

1. Starts with a left bracket [
2. Ends with a right bracket ]
3. Anything between the start and end must be
    - Empty, e.g. []
    - Valid MRON values separated by whitespace

##### CSV-style list of records

1. Before the left bracket [, a header (of the Header type) must be defined
2. The header starts with a left parenthesis (
3. The header ends with a right parenthesis )
4. The content between the left and right parenthesis should be whitespace-separated keys (of the Key type)
5. There must be at least one key

Once the rules are met, the values inside the list will be mapped to records. See the examples below.

## Examples

MRON is directly translatable to JSON. The examples below demonstrate how MRON compares to JSON.

```mron
# This is a single-line comment

###
This is a multi-line
comment!
###

name        "Alice"
age         25
is-adult    yes

address {
    country "UK"
    city    "London"
    street  "123 Baker Street"
}

hobbies     [ "soccer" "violin" ]

friends (name age) [
    "Bob" 26
    "Charlie" 31
]
```

The above MRON translates to the below JSON (pretty-printed with jq):

```json
{
    "name": "Alice",
    "age": 25,
    "is-adult": true,
    "address": {
        "country": "UK",
        "city": "London",
        "street": "123 Baker Street"
    },
    "hobbies": [ "soccer", "violin" ],
    "friends" [
        {"name": "Bob", "age": 26},
        {"name": "Charlie", "age": 31}
    ]
}
```
