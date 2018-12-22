#!/bin/bash
../Release/scorescan sheetRR2.svg 1 | ./parseRoverRuckus.py >> $1
