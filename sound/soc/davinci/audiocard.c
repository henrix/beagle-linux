/*
 * ASoC Driver for Davinci AudioCard platform
 *
 * Author:	Henrik Langer <henni19790@googlemail.com>
 *        	based on 
 * 				ASoC driver for TI DAVINCI EVM platform by Vladimir Barinov <vbarinov@embeddedalley.com>
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
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/platform_data/edma.h>
#include <linux/i2c.h>
#include <linux/of_platform.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-dapm.h>

#include <asm/dma.h>
#include <asm/mach-types.h>

#include <linux/edma.h>

#include "davinci-pcm.h"
#include "davinci-i2s.h"
#include "davinci-mcasp.h"

#include <linux/of_gpio.h>

#include "../codecs/ad193x.h"

#define AUDIOCARD_AD193X_DAIFMT ( SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_CBM_CFM )

/* sound card init */
static int snd_davinci_audiocard_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata_davinci *drvdata =
		snd_soc_card_get_drvdata(soc_card);

	// set codec DAI slots, 8 channels, all channels are enabled
	// codec driver ignores TX / RX mask and width
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0xFF, 0xFF, 2, 32);
	if (ret < 0){
		dev_err(codec->dev, "Unable to set AD193x TDM slots.");
		return ret;
	}

	/* May also set tdm slots of davinci-mcasp here. */

	/*ret = snd_soc_dai_set_clkdiv(cpu_dai, 0, 1);
	if (ret < 0)
		return ret;

	dev_dbg(soc_card->dev, "audiocard_init(): Sysclk from drvdata: %d", drvdata->sysclk);
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, drvdata->sysclk, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;*/

	return 0;
}

/* set hw parameters */
static int snd_davinci_audiocard_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *soc_card = rtd->card;
	struct device_node *np = soc_card->dev->of_node;
	int ret = 0;
	unsigned int sysclk;

	if (np) {
		ret = of_property_read_u32(np, "audiocard,codec-clock-rate", &sysclk);
		if (ret < 0){
			dev_err(cpu_dai->dev, "Unable to get codec clock rate from dtb: %d", ret);
			return ret;
		}
	} else {
		dev_err(cpu_dai->dev, "Error parsing device tree node: %d", np);
	}

	/* Set codec DAI configuration. */
	ret = snd_soc_dai_set_fmt(codec_dai, AUDIO_FORMAT);
	if (ret < 0){
		dev_err(codec->dev, "Unable to set AD193X DAI format: %d", ret);
		return ret;
	}

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, AUDIO_FORMAT);
	if (ret < 0){
		dev_err(cpu_dai, "Unable to set CPU DAI format: %d", ret);
		return ret;
	}

	/* set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, codec_sysclk, SND_SOC_CLOCK_IN); // clk_id and direction is ignored in ad193x driver
	if (ret < 0){
		dev_err(codec->dev, "Unable to set AD193x system clock: %d", ret);
		return ret;
	}

	/* set the CPU system clock */
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, codec_sysclk, SND_SOC_CLOCK_IN); 
	if (ret < 0){
		dev_err(cpu_dai->dev, "Unable to set cpu dai sysclk: %d", ret);
		return ret;
	}

	//Set bclk divider
	/*ret = snd_soc_dai_set_clkdiv(cpu_dai, 1, 256); 
	if (ret < 0){
		dev_err(cpu_dai->dev, "Unable to set cpu dai sysclk: %d", ret);
		return ret;
	}

	//Set bclk/lrclk ratio to 256 (8 channels)
	ret = snd_soc_dai_set_clkdiv(cpu_dai, 2, 64); 
	if (ret < 0){
		dev_err(cpu_dai->dev, "Unable to set cpu dai sysclk: %d", ret);
		return ret;
	}*/

	unsigned int sample_bits = snd_pcm_format_physical_width(params_format(params));
	dev_dbg(codec->dev, "audiocard hwparams(): sample_bits from params: %d\n", sample_bits);

	//Can be wrong for davinci-mcasp (not tested)
	//return snd_soc_dai_set_bclk_ratio(cpu_dai, 32*2); //64 Bit for 2 channels with fixes width

	//May need to set sysclk of cpu dai here
	return 0;
}

/* startup */
static int snd_davinci_audiocard_startup(struct snd_pcm_substream *substream) {
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata_davinci *drvdata = snd_soc_card_get_drvdata(soc_card);

	return 0;
}

/* shutdown */
static void snd_davinci_audiocard_shutdown(struct snd_pcm_substream *substream) {
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata_davinci *drvdata = snd_soc_card_get_drvdata(soc_card);
}

/* machine stream operations */
static struct snd_soc_ops snd_davinci_audiocard_ops = {
	.hw_params = snd_davinci_audiocard_hw_params,
	.startup = snd_davinci_audiocard_startup,
	.shutdown = snd_davinci_audiocard_shutdown,
};

/* interface setup */
static struct snd_soc_dai_link snd_davinci_audiocard_dai = {
	.name = "AudioCard",
	.stream_name = "AudioCard HiFi",
	.cpu_dai_name = "davinci-mcasp.0",
	.codec_dai_name ="ad193x-hifi",
	.platform_name = "davinci-mcasp.0",
	.codec_name = "spi0.0", //SPI0.CS0
	.ops = &snd_davinci_audiocard_ops,
	.init = snd_davinci_audiocard_init,
};

/* audio machine driver */
static struct snd_soc_card snd_davinci_audiocard = {
	.owner = THIS_MODULE,
	.name = "snd_davinci_audiocard",
	.dai_link = snd_davinci_audiocard_dai,
	.num_links = ARRAY_SIZE(snd_davinci_audiocard_dai),
};

#if defined(CONFIG_OF)

enum {
	MACHINE_VERSION_1 = 0,	/* Audiocard */
};

static const struct of_device_id snd_davinci_audiocard_dt_ids[] = {
	{
		.compatible = "audiocard,audiocard",
		.data = (void *)MACHINE_VERSION_1,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, snd_davinci_audiocard_dt_ids);

/* sound card test */
static int snd_davinci_audiocard_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match =
		of_match_device(of_match_ptr(snd_davinci_audiocard_dt_ids), &pdev->dev);
	u32 machine_ver, clk_gpio;
	int ret = 0;

	dev_dbg(&pdev->dev, "davinci_audiocard_probe called.\n");

	/*machine_ver = (u32)match->data;
	if (machine_ver = (u32)match->data == MACHINE_VERSION_1){

	}*/

	snd_davinci_audiocard_dai.codec_of_node = of_parse_phandle(np, "audiocard,audio-codec", 0);
	if (!snd_davinci_audiocard_dai.codec_of_node){
		dev_err(&pdev->dev, "Failed to get audio codec device tree node");
		return -EINVAL;
	}

	snd_davinci_audiocard_dai.cpu_of_node = of_parse_phandle(np, "audiocard,mcasp-controller", 0);
	if (!snd_davinci_audiocard_dai.cpu_of_node){
		dev_err(&pdev->dev, "Failed to get CPU mcasp device tree node");
		return -EINVAL;
	}

	snd_davinci_audiocard_dai.platform_of_node = snd_davinci_audiocard_dai.cpu_of_node;

	snd_davinci_audiocard.dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(&snd_davinci_audiocard, "audiocard,model"):
	if (ret){
		dev_err(&pdev->dev, "Failed to parse card name: %d", ret);
		return ret;
	}

	ret = snd_soc_register_card(&snd_davinci_audiocard);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);

	return ret;
}

/* sound card disconnect */
static int snd_davinci_audiocard_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	return snd_soc_unregister_card(card);
}


/* sound card platform driver */
static struct platform_driver snd_davinci_audiocard_driver = {
	.driver = {
		.name   = "snd-rpi-audiocard",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(snd_davinci_audiocard_dt_ids),
	},
	.probe          = snd_davinci_audiocard_probe,
	.remove         = snd_davinci_audiocard_remove,
};
#endif

static struct platform_device *audicard_snd_device;

static __init audicard_init(void)
{
	struct snd_soc_card *audicard_snd_dev_data;
	int index;
	int ret;

#if defined(CONFIG_OF)
	/*
	 * If dtb is there, the devices will be created dynamically.
	 * Only register platfrom driver structure.
	 */
	if (of_have_populated_dt())
		return platform_driver_register(&snd_davinci_audiocard_driver);
#endif

	audicard_snd_dev_data = &snd_davinci_audiocard;

	audicard_snd_device = platform_device_alloc("soc-audio", 0); //Index = 1?
	if (!audicard_snd_device)
		return -ENOMEM;

	platform_set_drvdata(audicard_snd_device, audicard_snd_dev_data);
	ret = platform_device_add(audicard_snd_device);
	if (ret)
		platform_device_put(udicard_snd_device)

	return ret;
}

static void __exit audiocard_exit(void){
#if defined(CONFIG_OF)
	if (of_have_populated_dt()) {
		platform_driver_unregister(&snd_davinci_audiocard_driver);
		return;
	}
#endif

	platform_device_unregister(audicard_snd_device);
}

module_init(audicard_init);
module_exit(audiocard_exit);

/* Module information */
MODULE_AUTHOR("Henrik Langer");
MODULE_DESCRIPTION("ALSA SoC AD193X board driver");
MODULE_LICENSE("GPL");
