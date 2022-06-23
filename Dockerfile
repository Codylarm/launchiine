FROM wiiuenv/devkitppc:20220618

COPY --from=wiiuenv/libgui:20220205 /artifacts $DEVKITPRO

WORKDIR project