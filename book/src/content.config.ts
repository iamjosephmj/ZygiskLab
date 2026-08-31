import { defineCollection } from 'astro:content';
import { docsLoader } from '@astrojs/starlight/loaders';
import { docsSchema } from '@astrojs/starlight/schema';
import { z } from 'astro/zod';

export const collections = {
  docs: defineCollection({
    loader: docsLoader(),
    schema: docsSchema({
      // Verification state of a chapter. `proven` means the procedure was run
      // on the reference rig; nothing is promoted on a clean compile alone.
      extend: z.object({
        status: z.enum(['proven', 'unverified']).default('unverified'),
      }),
    }),
  }),
};
