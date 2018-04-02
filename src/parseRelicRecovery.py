#!/usr/bin/python3
import json
import sys
import os

THRESHOLD = 0.3

def parseOption(data, key):
    keys = [(k, v) for (k, v) in data.items() if k.startswith(key + '.')]
    bestResult = max(keys, key=lambda item: item[1])
    if bestResult[1] > THRESHOLD:
        return bestResult[0][len(key) + 1:]

def parseOptionOrDefault(data, key, default):
    result = parseOption(data, key)
    return default if result is None else result

def parseNumberOrDefault(data, digitKeys, default):
    digits = []
    for k in digitKeys:
        option = parseOption(data, k)
        if option is not None:
            digits.append(option)
    return int(''.join(digits)) if len(digits) > 0 else default

def parseVector(data, keys):
    return [data[k] > THRESHOLD for k in keys]

def countRows(cryptobox):
    return sum([1 if all(cryptobox[i:i+3]) else 0 for i in range(0, 12, 3)])

def countColumns(cryptobox):
    return sum([1 if all(cryptobox[i::3]) else 0 for i in range(3)])

def countGlyphs(cryptobox):
    return sum([1 if cryptobox[i] else 0 for i in range(12)])

cryptobox1IDs = ["box1." + str(i) for i in range(1, 13)]
cryptobox2IDs = ["box2." + str(i) for i in range(1, 13)]

for line in sys.stdin:
    data = json.loads(line)

    relic1Pos = int(parseOptionOrDefault(data, "relic1", 0))
    relic2Pos = int(parseOptionOrDefault(data, "relic2", 0))

    box1 = parseVector(data, cryptobox1IDs)
    box2 = parseVector(data, cryptobox2IDs)

    results = {
        "match" : parseNumberOrDefault(data, ["match1", "match2", "match3"], ""),
        "team" : parseNumberOrDefault(data, ["team1", "team2", "team3", "team4", "team5"], ""),
        "color" : parseOptionOrDefault(data, "color", ''),
        "autoglyphs" : parseOptionOrDefault(data, "autoglyphs", 0),
        "jewel" : int(parseOptionOrDefault(data, "jewel", 0)),
        "key" : int(parseOptionOrDefault(data, "key", 0)),
        "autopark" : int(parseOptionOrDefault(data, "autopark", 0)),
        "cipher" : int(parseOptionOrDefault(data, "cipher1", 0)) + int(parseOptionOrDefault(data, "cipher2", 0)),
        "rows" : countRows(box1) + countRows(box2),
        "columns" : countColumns(box1) + countColumns(box2),
        "glyphs" : countGlyphs(box1) + countGlyphs(box2),
        "balanced" : int(parseOptionOrDefault(data, "balanced", 0)),
        "zone1relics" : (1 if relic1Pos is 1 else 0) + (1 if relic2Pos is 1 else 0),
        "zone2relics" : (1 if relic1Pos is 2 else 0) + (1 if relic2Pos is 2 else 0),
        "zone3relics" : (1 if relic1Pos is 3 else 0) + (1 if relic2Pos is 3 else 0),
        "standingRelics" : int(parseOptionOrDefault(data, "relic1standing", 0)) + int(parseOptionOrDefault(data, "relic2standing", 0))
    }

    headers = ','.join(map(str, results.keys()))
    data = ','.join(map(str, results.values()))
    print(data)
    if len(sys.argv) > 1:
        filename = sys.argv[1]
        exists = os.path.exists(filename)
        with open(filename, "a+") as f:
            if not exists:
                f.write(headers + "\n")
            f.write(data + "\n")