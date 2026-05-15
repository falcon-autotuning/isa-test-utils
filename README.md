### SIM900 instrument driver for the instrument-script-server

# Test Runtime Environment

Some tests are hardware-backed and require runtime environment variables.

Required:

```bash
export SIM900_ADDRESS
export SIM_CHANNEL
```

Then run:

```bash
make test
```
