# Ams Env
(Amstrad Environment)

A CPC 464 environment for reinforcement learning.
I had this amazing compute during my childhood and what I learned to program on.

Many years ago OpenAI built a framework for AI to learn to play retro games.
https://openai.com/index/gym-retro/

This only really covered the main consoles.

They use a wrapper around libretro cores to support many platforms.
I've tried to update the code to run amstrad cores but I could never really get it working.

I really like the api that retro used and this has been generalised by the gymnasium library
https://gymnasium.farama.org/index.html

With the help of gpt I can build my own wrapper around the amstrad libretro core and expose that through gymnasium.

Thats the plan anyway...

## Setup

Download https://buildbot.libretro.com/nightly/windows/x86_64/latest/cap32_libretro.dll.zip
Put cap32_libretro.dll into the /cores folder

Find and download the system rom cpc464.rom and put it in the /system folder

Download an amstrad rom and put it in the roms folder

Run `Make` to build the capriceenv.dll file

## Run

An 
