# Effects for the prop and flora cursors. All textures are the game's own.
#
# Build into the plugin's package with effdir-compile from sc4-effects-extensions:
#   uv run effdir-compile SC4PlopAndPaintEffects.fx -o SC4PlopAndPaint.dat --instance 0x5CBCFC00 --compress
#
# life and -speed take a base value and a variation, not a minimum and a maximum.

textureID pnp_footprint 0x13c14520
modelID   pnp_model     0x02010000

# As vanilla preview_el_straight, without -pinToTerrain so raised items stay at their height.
particles pnp_cursor_model
    life 0.1
    emit -inject 1 -loop 0.1 0
    source -point
    alpha 0.5
    size 1
    rotate 1
    model pnp_model -noCullFaces
end

# A see-through copy of the placed model that grows and fades. Small items grow more, so the
# growth stays visible.
particles pnp_pop_small
    life 0.35
    emit -inject 1 -single 1
    source -point
    alpha 0.55 0
    size 1 1.6
    rotate 1
    model pnp_model -noCullFaces
end

particles pnp_pop_medium
    life 0.35
    emit -inject 1 -single 1
    source -point
    alpha 0.55 0
    size 1 1.3
    rotate 1
    model pnp_model -noCullFaces
end

particles pnp_pop_large
    life 0.35
    emit -inject 1 -single 1
    source -point
    alpha 0.55 0
    size 1 1.14
    rotate 1
    model pnp_model -noCullFaces
end

# Vanilla building_footprint_ok in red. Vanilla building_footprint_notok has no additive draw, so
# it is opaque unless the game gives it a texture with alpha.
decal pnp_footprint_notok
    color "1 0.1 0.1"
    alpha 0.5
    life 5 -loop
    texture pnp_footprint -light -draw additive
end

decal pnp_footprint_notok_invert
    color "1 0.6 0.6"
    alpha 0.2
    life 5 -loop
    texture pnp_footprint -light -draw decalInvertDepth
end

effect pnp_preview_model
    particleEffect pnp_cursor_model
end

effect pnp_pop_small
    particleEffect pnp_pop_small
end

effect pnp_pop_medium
    particleEffect pnp_pop_medium
end

effect pnp_pop_large
    particleEffect pnp_pop_large
end

effect pnp_footprint_notok
    decalEffect pnp_footprint_notok
    decalEffect pnp_footprint_notok_invert
end
