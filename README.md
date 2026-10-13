# Elden Ring Auto Stat Randomizer

A DLL mod for Elden Ring that automatically rerolls all 8 stats randomly whenever you level up. 
Total points stay at level + 79 and each stat is capped at 99.

## Use

1. Add `randomizer.dll` to `external_dlls` in the ModEngine 2 config.
2. Launch with EAC disabled. Don't go online with it.

## Hotkey

Press **F9** to force a reroll at any time, without needing to level up. Handy for testing.

## How the randomization works

1. On level up, the mod takes your total points to distribute: level + 79.
2. It repeatedly picks one of the 8 stats at random and gives it a chunk of the remaining points (remaining / 8, minimum 1).
3. Chunks get smaller as the pool shrinks, so you get a few big spikes and a spread of smaller bumps.
4. A stat can't go above 99. Any overflow goes to another stat.
5. This repeats until all points are spent, so the total always matches your level.

## Notes

- If you're in the level-up menu, you need to re-enter the level-up menu to see the change in stats.