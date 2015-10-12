/*
 * ASoC Driver for AudioCard
 *
 * Author:	Henrik Langer <henni19790@googlemail.com>
 *		Copyright 2015
 *        	based on 
 * 				RaspiDAC3 driver by Jan Grulich <jan@grulich.eu>,
 *				BlackFin-AD193x driver by Barry Song <Barry.Song@analog.com>
 *				SuperAudioBoard driver by R F William Hollender <whollender@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#define DEBUG 1
#include <linux/module.h>
//#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-dapm.h>

#include "../codecs/ad193x.h"

/* sound card init */
static int snd_rpi_audiocard_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *card = rtd->card;
	struct ad193x_priv *ad193x = snd_soc_codec_get_drvdata(codec);

	// set codec DAI slots, 8 channels, all channels are enabled
	// codec driver ignores TX / RX mask and width
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xFF, 0xFF, 2, 32);
	if (ret < 0){
		dev_err(codec->dev, "Unable to set AD193x TDM slots.");
		return ret;
	}

	// (ad193x driver only supports SND_SOC_DAIFMT_DSP_A and SND_SOC_DAIFMT_I2S with TDM)
	/*ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_IF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0){
		dev_err(codec->dev, "Unable to set codec DAI format.");
		return ret;
	}*/

	// cpu DAI only supports SND_SOC_DAIFMT_I2S (see bcm2835-i2s.c)
	// TODO: for multichannel support, platform driver have to be modified
	/*ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0){
		dev_err(card->dev, "Unable to set cpu DAI format.");
		return ret;
	}*/

	return 0;
}

/* set hw parameters */
static int snd_rpi_audiocard_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	int ret;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;

	// Need to tell the codec what it's system clock is (12.288 MHz crystal)
	int sysclk = 12288000;

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, sysclk, SND_SOC_CLOCK_IN); // clk_id is ignored in ad193x driver
	if (ret < 0){
		dev_err(codec->dev, "Unable to set AD193x system clock.");
		return ret;
	}

	unsigned int sample_bits = snd_pcm_format_physical_width(params_format(params));
	dev_dbg(codec->dev, "audiocard hwparams(): sample_bits from params: %d\n", sample_bits);

	return snd_soc_dai_set_bclk_ratio(cpu_dai, 32*2); //64 Bit for 2 channels
}

/* startup */
static int snd_rpi_audiocard_startup(struct snd_pcm_substream *substream) {
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;

	return 0;
}

/* shutdown */
static void snd_rpi_audiocard_shutdown(struct snd_pcm_substream *substream) {
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
}

/* machine stream operations */
static struct snd_soc_ops snd_rpi_audiocard_ops = {
	.hw_params = snd_rpi_audiocard_hw_params,
	.startup = snd_rpi_audiocard_startup,
	.shutdown = snd_rpi_audiocard_shutdown,
};

/* interface setup */
#define AUDIOCARD_AD193X_DAIFMT ( SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_IF | SND_SOC_DAIFMT_CBM_CFM )

static struct snd_soc_dai_link snd_rpi_audiocard_dai[] = {
	{
		.name = "AudioCard",
		.stream_name = "AudioCard HiFi",
		.cpu_dai_name = "bcm2708-i2s.0",
		.codec_dai_name ="ad193x-hifi",
		.platform_name = "bcm2708-i2s.0",
		.codec_name = "spi0.0", //CS GPIO 8 
		.dai_fmt = AUDIOCARD_AD193X_DAIFMT,
		.ops = &snd_rpi_audiocard_ops,
		.init = snd_rpi_audiocard_init,
	},
};

/* audio machine driver */
static struct snd_soc_card snd_rpi_audiocard = {
	.name = "snd_rpi_audiocard",
	.dai_link = snd_rpi_audiocard_dai,
	.num_links = ARRAY_SIZE(snd_rpi_audiocard_dai),
};

/* sound card test */
static int snd_rpi_audiocard_probe(struct platform_device *pdev)
{
	int ret = 0;

	snd_rpi_audiocard.dev = &pdev->dev;

	if (pdev->dev.of_node) {
	    struct device_node *i2s_node;
	    struct snd_soc_dai_link *dai = &snd_rpi_audiocard_dai[0];
	    i2s_node = of_parse_phandle(pdev->dev.of_node,
					"i2s-controller", 0);

	    if (i2s_node) {
		dai->cpu_dai_name = NULL;
		dai->cpu_of_node = i2s_node;
		dai->platform_name = NULL;
		dai->platform_of_node = i2s_node;
	    }
	}

	ret = snd_soc_register_card(&snd_rpi_audiocard);
	if (ret)
		dev_err(&pdev->dev,
			"snd_soc_register_card() for ad1938 failed: %d\n", ret);

	return ret;
}

/* sound card disconnect */
static int snd_rpi_audiocard_remove(struct platform_device *pdev)
{
	return snd_soc_unregister_card(&snd_rpi_audiocard);
}

static const struct of_device_id snd_rpi_audiocard_of_match[] = {
	{ .compatible = "audiocard,audiocard", },
	{},
};
MODULE_DEVICE_TABLE(of, snd_rpi_audiocard_of_match);

/* sound card platform driver */
static struct platform_driver snd_rpi_audiocard_driver = {
	.driver = {
		.name   = "snd-rpi-audiocard",
		.owner  = THIS_MODULE,
		.of_match_table = snd_rpi_audiocard_of_match,
	},
	.probe          = snd_rpi_audiocard_probe,
	.remove         = snd_rpi_audiocard_remove,
};

module_platform_driver(snd_rpi_audiocard_driver);

/* Module information */
MODULE_AUTHOR("Henrik Langer");
MODULE_DESCRIPTION("ALSA SoC AD193X board driver");
MODULE_LICENSE("GPL");
