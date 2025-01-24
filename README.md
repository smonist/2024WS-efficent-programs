# Join

Run `make` to build the `ourJoin` executable.

After that, use the script:
- `--small`: Runs the command with the small files to verify that we didn't break the implementation
- `--profile`: Runs perf stat.
- `--recompile`: Calls clean and make before running
- No options to run it on the large files

```bash
./script.sh --small --profile --recompile
```
