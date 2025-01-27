# Join

Run `make` to build the `ourJoin` executable.

After that, use the script:
- `--small`: Runs the command with the small files to verify that we didn't break the implementation
- `--profile`: Runs perf stat.
- `--recompile`: Calls clean and make before running
- `--validate`: Check results against csvs
- No options to run it on the large files

```bash
./run.sh --small --profile --recompile
```

[https://www.complang.tuwien.ac.at/anton/lvas/effizienz-aufgabe24/](https://www.complang.tuwien.ac.at/anton/lvas/effizienz-aufgabe24/)  
[https://www.complang.tuwien.ac.at/anton/lvas/effizienz-abgaben/2024w/](https://www.complang.tuwien.ac.at/anton/lvas/effizienz-abgaben/2024w/)