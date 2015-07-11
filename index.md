# Tyrutils-ericw


# Features



## Ambient Occlusion



### Dirtdepth

Controls how many units around each point on the lightmap we check for occlusion. Default 128.
At higher values like 1024, you can see the palace interior starts to self-shadow.
 
Command-line: `-dirtdepth`, worldspawn key: `_dirtdepth`

<img src="dirtdepth_128.jpg" width="1024" height="768"/>
<img src="dirtdepth_256.jpg" width="1024" height="768"/>
<img src="dirtdepth_512.jpg" width="1024" height="768"/>
<img src="dirtdepth_1024.jpg" width="1024" height="768"/>

### Dirtgain

<img src="dirtgain_0.5.jpg" width="1024" height="768"/>
<img src="dirtgain_0.75.jpg" width="1024" height="768"/>
<img src="dirtgain_1.0.jpg" width="1024" height="768"/>

### Dirtmode

<img src="dirtmode_0_dirtgain_0.5.jpg" width="1024" height="768"/>
<img src="dirtmode_1_dirtgain_0.5.jpg" width="1024" height="768"/>

### Dirtscale

<img src="dirtscale_1.0.jpg" width="1024" height="768"/>
<img src="dirtscale_1.5.jpg" width="1024" height="768"/>
<img src="dirtscale_2.0.jpg" width="1024" height="768"/>

## (Fake) Area Lights

<img src="deviance_0.jpg" width="1024" height="768"/>
<img src="deviance_24.jpg" width="1024" height="768"/>
<img src="deviance_48.jpg" width="1024" height="768"/>
<img src="deviance_96.jpg" width="1024" height="768"/>

```
{
"delay" "2"
"light" "600"
"origin" "376 0 408"
"classname" "light_globe"
}
```

```
{
"_deviance" "24"
"delay" "2"
"light" "600"
"origin" "376 0 408"
"classname" "light_globe"
}
```

```
{
"_deviance" "48"
"delay" "2"
"light" "600"
"origin" "376 0 408"
"classname" "light_globe"
}
```

```
{
"_deviance" "96"
"delay" "2"
"light" "600"
"origin" "376 0 408"
"classname" "light_globe"
}
```

## Sunlight2

```
"_sunlight_color" "0.83 0.91 1.00"
"_sun_mangle" "60 -45 0"
"_sunlight" "300"
"_sunlight_penumbra" "4"
```

```
"_sunlight2" "300"
```

```
"_sunlight_color" "0.83 0.91 1.00"
"_sun_mangle" "60 -45 0"
"_sunlight" "300"
"_sunlight_penumbra" "4"
"_sunlight2" "300"
```


<img src="a_sunlight.jpg" width="1024" height="768"/> <img src="b_sunlight.jpg" width="1024" height="768"/>

<img src="a_sunlight_plus_sunlight2.jpg" width="1024" height="768"/> <img src="b_sunlight_plus_sunlight2.jpg" width="1024" height="768"/>

<img src="a_sunlight2.jpg" width="1024" height="768"/> <img src="b_sunlight2.jpg" width="1024" height="768"/>


## Sunlight Penumbra

```
"_sunlight_color" "0.83 0.91 1.00"
"_sun_mangle" "60 -45 0"
"_sunlight" "300"
```

```
"_sunlight_color" "0.83 0.91 1.00"
"_sun_mangle" "60 -45 0"
"_sunlight" "300"
"_sunlight_penumbra" "2"
```

```
"_sunlight_color" "0.83 0.91 1.00"
"_sun_mangle" "60 -45 0"
"_sunlight" "300"
"_sunlight_penumbra" "4"
```

```
"_sunlight_color" "0.83 0.91 1.00"
"_sun_mangle" "60 -45 0"
"_sunlight" "300"
"_sunlight_penumbra" "8"
```



<img src="sunlight_penumbra_0.jpg" width="1024" height="768"/>
<img src="sunlight_penumbra_2.jpg" width="1024" height="768"/>
<img src="sunlight_penumbra_4.jpg" width="1024" height="768"/>
<img src="sunlight_penumbra_8.jpg" width="1024" height="768"/>

## Surface Lights

<img src="surface.jpg" width="1024" height="768"/>

```
{
"classname" "light"
"origin" "-640 -192 656" // ignored for surface lights
"delay" "2"
"light" "150"
"_color" "0.827111 0.908589 1.000000"
"_surface" "runes1a" // texture name
}
```
