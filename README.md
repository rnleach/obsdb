# obsdb
A library for downloading and archiving SynopticLabs weather data.

# Motivation

This is for creating an archive of observations that I have downloaded from the [Synoptic Labs API](https://developers.synopticdata.com/mesonet/), and downloading more if I request them. I use mainly use the downloaded observations for verifying forecasts.

# Documentation

Doxygen comments and a [Doxyfile](Doxyfile) are included. For hacking on the library internals, set the `INPUT` variable in [Doxyfile](Doxyfile) (around line 730) to include all of `src/`. For library users, set the `INPUT` variable to `src/obs.h` and only the public API will be documented.
