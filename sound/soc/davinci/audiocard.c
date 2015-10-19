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
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-dapm.h>

#include "../codecs/ad193x.h"

struct snd_soc_card_drvdata_davinci {
	struct clk *mclk;
	unsigned sysclk;
};

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

	ret = snd_soc_dai_set_clkdiv(cpu_dai, 0, 1);
	if (ret < 0)
		return ret;

	dev_dbg(soc_card->dev, "audiocard_init(): Sysclk from drvdata: %d", drvdata->sysclk);
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, drvdata->sysclk, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

/* set hw parameters */
static int snd_davinci_audiocard_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *soc_card = rtd->card;
	unsigned sysclk = ((struct snd_soc_card_drvdata_davinci *) snd_soc_card_get_drvdata(soc_card))->sysclk; // = 12,244 MHz


	// Need to tell the codec what it's system clock is (12.288 MHz crystal)
	int codec_sysclk = 12288000;
	int cpu_dai_sysclk = 24576000;

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, codec_sysclk, SND_SOC_CLOCK_IN); // clk_id and direction is ignored in ad193x driver
	if (ret < 0){
		dev_err(codec->dev, "Unable to set AD193x system clock: %d", ret);
		return ret;
	}

	dev_dbg(cpu_dai->dev, "audiocard_hw_params(): CPU DAI sysclk = %d", sysclk);	
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

	if (drvdata->mclk)
		return clk_prepare_enable(drvdata->mclk);

	return 0;
}

/* shutdown */
static void snd_davinci_audiocard_shutdown(struct snd_pcm_substream *substream) {
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata_davinci *drvdata =
		snd_soc_card_get_drvdata(soc_card);

	if (drvdata->mclk)
		clk_disable_unprepare(drvdata->mclk);
}

/* machine stream operations */
static struct snd_soc_ops snd_davinci_audiocard_ops = {
	.hw_params = snd_davinci_audiocard_hw_params,
	.startup = snd_davinci_audiocard_startup,
	.shutdown = snd_davinci_audiocard_shutdown,
};

/* interface setup */
#define AUDIOCARD_AD193X_DAIFMT ( SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_CBM_CFM )

static struct snd_soc_dai_link snd_davinci_audiocard_dai[] = {
	{
		.name = "AudioCard",
		.stream_name = "AudioCard HiFi",
		.cpu_dai_name = "davinci-mcasp.0",
		.codec_dai_name ="ad193x-hifi",
		.platform_name = "davinci-mcasp.0",
		.codec_name = "spi0.0", //SPI0.CS0
		.dai_fmt = AUDIOCARD_AD193X_DAIFMT,
		.ops = &snd_davinci_audiocard_ops,
		.init = snd_davinci_audiocard_init,
	},
};

static const struct of_device_id snd_davinci_audiocard_dt_ids[] = {
	{
		.compatible = "audiocard,audiocard",
		.data = &snd_davinci_audiocard_dai,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, snd_davinci_audiocard_dt_ids);

/* audio machine driver */
static struct snd_soc_card snd_davinci_audiocard = {
	.owner = THIS_MODULE,
	.name = "snd_davinci_audiocard",
	.dai_link = snd_davinci_audiocard_dai,
	.num_links = ARRAY_SIZE(snd_davinci_audiocard_dai),
};

/* sound card test */
static int snd_davinci_audiocard_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match =
		of_match_device(of_match_ptr(snd_davinci_audiocard_dt_ids), &pdev->dev);
	struct snd_soc_dai_link *dai = (struct snd_soc_dai_link *) match->data;
	struct snd_soc_card_drvdata_davinci *drvdata = NULL;
	struct clk *mclk;
	int ret = 0;

	dev_dbg(&pdev->dev, "davinci_audiocard_probe called.\n");

	snd_davinci_audiocard.dai_link = dai;

	dai->codec_name = NULL;
	dai->codec_of_node = of_parse_phandle(np, "audio-codec", 0);
	if (!dai->codec_of_node)
		return -EINVAL;

	dai->cpu_dai_name = NULL;
	dai->cpu_of_node = of_parse_phandle(np, "mcasp-controller", 0);

	dai->platform_name = NULL;
	if (!dai->cpu_of_node)
		return -EINVAL;

	dai->platform_of_node = dai->cpu_of_node;

	snd_davinci_audiocard.dev = &pdev->dev;

	mclk = devm_clk_get(&pdev->dev, "mclk");
	if (PTR_ERR(mclk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(mclk)) {
		dev_dbg(&pdev->dev, "mclk not found.\n");
		mclk = NULL;
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->mclk = mclk;

	ret = of_property_read_u32(np, "codec-clock-rate", &drvdata->sysclk);

	if (ret < 0) {
		if (!drvdata->mclk) {
			dev_err(&pdev->dev,
				"No clock or clock rate defined.\n");
			return -EINVAL;
		}
		drvdata->sysclk = clk_get_rate(drvdata->mclk);
	} else if (drvdata->mclk) {
		unsigned int requestd_rate = drvdata->sysclk;
		clk_set_rate(drvdata->mclk, drvdata->sysclk);
		drvdata->sysclk = clk_get_rate(drvdata->mclk);
		if (drvdata->sysclk != requestd_rate)
			dev_warn(&pdev->dev,
				 "Could not get requested rate %u using %u.\n",
				 requestd_rate, drvdata->sysclk);
	}

	snd_soc_card_set_drvdata(&snd_davinci_audiocard, drvdata);
	/*ret = devm_snd_soc_register_card(&pdev->dev, &snd_davinci_audiocard);

	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);*/

	ret = snd_soc_register_card(&snd_davinci_audiocard);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n", ret);

	dev_dbg(&pdev->dev, "davinci_audiocard_probe finished.\n");

	return ret;
}

/* sound card disconnect */
static int snd_davinci_audiocard_remove(struct platform_device *pdev)
{
	return snd_soc_unregister_card(&snd_davinci_audiocard);
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

module_platform_driver(snd_davinci_audiocard_driver);

/* Module information */
MODULE_AUTHOR("Henrik Langer");
MODULE_DESCRIPTION("ALSA SoC AD193X board driver");
MODULE_LICENSE("GPL");
