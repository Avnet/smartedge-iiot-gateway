#!/bin/bash
mkdir bld
cd bld
cmake -Duse_prov_client=ON -Dhsm_type_sastoken=ON -Dno_logging=ON ..
cmake --build .

