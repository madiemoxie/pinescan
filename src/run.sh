#!/bin/bash
../Release/scorescan sheet.svg 1 | ./parseRelicRecovery.py >> $1
