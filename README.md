> **Work in progress**

This project is not ready for installation or use. It provides no deployment or compatibility guarantee.

# Playerbots Recovery

Playerbots Recovery is an AzerothCore module for detecting repeated movement and action loops, quarantining failed
objectives, and performing explicit audited homebind recovery. It keeps its state outside mod-playerbots and consumes
only generic bot update, action, death, removal, and objective decision hooks.

The module does not change standard Wrath content rules. It does not automatically teleport a bot merely because an
anomaly was observed. A recovery happens only through the registered recovery action or a caller of the public
recovery service.

## Dependencies

* A Playerbot compatible AzerothCore checkout
* The public mod-playerbots fork with the generic extension registry

## Standalone verification

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

## License

Playerbots Recovery is licensed under the GNU General Public License version 2. See `LICENSE`.
