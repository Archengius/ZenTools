# ZenTools

ZenTools extracts cooked packages (.uasset/.uexp) from the IoStore container files (.ucas/.utoc + .pak).

Works on UE5.1 and 5.2 and should support legacy 5.1 archives too.

## Usage:

`ZenTools.exe ExtractPackages <ContainerFolderPath> <ExtractionDir> [-EncryptionKeys=<KeyFile>]`

If your game has encrypted paks, you must provide a keys.json, in the following format:

```json
{
  "KeyGUID1": "KeyHex1",
  "KeyGUID2": "KeyHex2"
}
```

Obviously if your game only has one encryption key, you only need to specify one entry.

**Example:**

`ZenTools.exe ExtractPackages "D:\\SteamLibrary\\steamapps\\common\\somegame\\projectname\\Content\\Paks" "D:\\somegame\\Output" -EncryptionKeys="D:\\somegame\\keys.json"`

Since the game in the above example needs an AES key, this is the following `keys.json` file:

```json
{
  "00000000-0000-0000-0000-000000000000": "DEADBEEFCAFEDEADBEEFCAFEDEADBEEFCAFEDEADBEEFCAFEDEADBEEFCAFEDEAD"
}
```
